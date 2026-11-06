#pragma once
#include "BrokerJob.hpp"
namespace jade::native::broker {
class BrokerServer {
  public:
    BrokerStatePaths paths;
    BrokerRuntime::Executor execute;
    BrokerRuntime::Release release;
    std::atomic_bool stopped = false;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<BrokerJob>> queue;
    std::map<std::string, std::shared_ptr<BrokerJob>> jobs;
    std::map<std::string, std::pair<std::string, std::shared_ptr<BrokerProcess>>> helpers;
    std::string reservedHelper;
    unsigned connectionCount = 0;
    std::condition_variable connectionsClosed;
    HANDLE ownershipJob = nullptr;
    json nativePids() const {
        for (size_t capacity = 16; capacity <= 65536; capacity *= 2) {
            std::vector<unsigned char> bytes(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
                                             capacity * sizeof(ULONG_PTR));
            auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(bytes.data());
            if (!QueryInformationJobObject(ownershipJob, JobObjectBasicProcessIdList, list,
                                           static_cast<DWORD>(bytes.size()), nullptr)) {
                if (GetLastError() == ERROR_MORE_DATA)
                    continue;
                throw windowsError("Cannot query owned native Jade processes");
            }
            json pids = json::array();
            for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
                const DWORD pid = static_cast<DWORD>(list->ProcessIdList[i]);
                if (pid == GetCurrentProcessId())
                    continue;
                HANDLE process =
                    OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (!process)
                    continue;
                BOOL owned = FALSE;
                if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
                    IsProcessInJob(process, ownershipJob, &owned) && owned)
                    pids.push_back(pid);
                CloseHandle(process);
            }
            return pids;
        }
        throw std::runtime_error("Owned native process list exceeds its bounded status limit.");
    }
    void wake() {
        HANDLE pipe = openPipe(paths);
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
    }
    void result(const std::shared_ptr<BrokerJob> &job, const json &result) {
        job->channel->send(
            {{"id", job->request.at("id")}, {"event", "result"}, {"result", result}});
    }
    void dispatch(const std::shared_ptr<BrokerChannel> &channel, json request) {
        if (!request.is_object() || !request.contains("id") ||
            (!request["id"].is_string() && !request["id"].is_number_integer()) ||
            !request.contains("command") || !request["command"].is_string())
            throw std::runtime_error("Malformed broker request.");
        if (!equalSecret(request.value("token", ""), paths.secret))
            throw std::runtime_error("Broker authentication failed.");
        request.erase("token");
        auto job = std::make_shared<BrokerJob>();
        job->id =
            request["id"].is_string() ? request["id"].get<std::string>() : request["id"].dump();
        job->key = channel->owner + ":" + job->id;
        job->request = std::move(request);
        job->channel = channel;
        const auto rawCommand = job->request["command"].get<std::string>();
        const auto command = rawCommand == "cli" ? job->request.value("operation", "") : rawCommand;
        std::unique_lock lock(mutex);
        if (command == "status") {
            json active = json::array();
            for (auto &[_, j] : jobs)
                if (j->channel == channel)
                    active.push_back({{"id", j->id}, {"state", j->state}});
            result(job, {{"code", 0},
                         {"passed", true},
                         {"brokerPID", GetCurrentProcessId()},
                         {"nativePIDs", nativePids()},
                         {"activeJobs", active},
                         {"sdkVersion", paths.options.version},
                         {"fingerprint", paths.options.fingerprint}});
            return;
        }
        if (command != "shutdown" &&
            job->request.value("fingerprint", "") != paths.options.fingerprint) {
            result(job, {{"code", 2},
                         {"passed", false},
                         {"report",
                          {{"error", "Native broker artifact fingerprint mismatch. Finish active "
                                     "operations and restart the broker."}}}});
            return;
        }
        if (command == "cancel") {
            auto operation = job->request.value("operationId", json());
            std::string id =
                operation.is_string() ? operation.get<std::string>() : operation.dump();
            auto found = jobs.find(channel->owner + ":" + id);
            if (found != jobs.end())
                found->second->cancelled = true;
            result(job, {{"code", 0}, {"passed", true}, {"cancelled", found != jobs.end()}});
            return;
        }
        if (command == "helper.write" || command == "helper.end") {
            auto found = helpers.find(job->request.value("lease", ""));
            if (found == helpers.end() || found->second.first != channel->owner) {
                result(job, {{"code", 2},
                             {"passed", false},
                             {"report",
                              {{"error", "Native helper lease is not owned by this client."}}}});
                return;
            }
            auto helper = found->second.second;
            lock.unlock();
            if (command == "helper.end")
                helper->end();
            else
                helper->write(job->request.at("text").get<std::string>());
            return;
        }
        if (command == "shutdown") {
            for (auto &[_, existing] : jobs)
                existing->cancelled = true;
            for (auto &[_, helper] : helpers)
                helper.second->stop();
        } else if (!reservedHelper.empty()) {
            result(job, {{"code", 3},
                         {"passed", false},
                         {"busy", true},
                         {"report",
                          {{"error", "A native helper session owns the runtime. Close it before "
                                     "starting another native operation."}}}});
            return;
        }
        if (jobs.contains(job->key)) {
            result(job, {{"code", 2},
                         {"passed", false},
                         {"report", {{"error", "Duplicate active request id."}}}});
            return;
        }
        size_t owned = 0;
        for (const auto &[_, existing] : jobs)
            if (existing->channel == channel)
                ++owned;
        if (jobs.size() >= 256 || owned >= 32) {
            result(job, {{"code", 3},
                         {"passed", false},
                         {"busy", true},
                         {"report",
                          {{"error", "Native broker request queue is full. Wait for active "
                                     "operations to finish."}}}});
            return;
        }
        if (command == "helper.open")
            reservedHelper = job->key;
        if (command.starts_with("inspect.")) {
            job->externalSession = job->request.value("sessionId", "default");
            const auto session = channel->owner + ":" + job->externalSession;
            job->request["sessionId"] = session;
            // Keep ownership while a close is queued or can still be cancelled.
            // Disconnect cleanup must still see the native session in that case.
            channel->inspectionSessions.insert(session);
        }
        jobs[job->key] = job;
        queue.push_back(job);
        changed.notify_one();
    }
    void work() {
        for (;;) {
            std::shared_ptr<BrokerJob> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopped || !queue.empty(); });
                if (stopped && queue.empty())
                    return;
                job = queue.front();
                queue.pop_front();
                job->state = "running";
            }
            try {
                const auto command = job->request["command"].get<std::string>();
                if (job->cleanup) {
                    for (const auto &session : job->channel->inspectionSessions)
                        execute(
                            {{"command", "inspect.close"}, {"sessionId", session}},
                            [](const std::string &) {}, &job->cancelled);
                } else if (job->cancelled || job->channel->disconnected)
                    result(job, {{"code", 130}, {"passed", false}, {"cancelled", true}});
                else if (command == "shutdown") {
                    release();
                    result(job, {{"code", 0}, {"passed", true}});
                    stopped = true;
                    wake();
                } else if (command == "helper.open") {
                    const auto kind = job->request.value("kind", "");
                    const std::map<std::string, std::wstring> names = {
                        {"inspect", L"jade-inspect-helper.exe"},
                        {"debug", L"jade-debug-helper.exe"},
                        {"compiler", L"jade-native-helper.exe"}};
                    if (!names.contains(kind))
                        throw std::runtime_error("Unsupported native helper.");
                    std::vector<std::wstring> args;
                    if (kind != "compiler") {
                        auto session = std::filesystem::path(
                            wide(job->request.at("session").get<std::string>()));
                        std::ifstream file(session);
                        json config;
                        file >> config;
                        if (!config.contains("installPath") || !config.contains("databasePath"))
                            throw std::runtime_error(
                                "Incomplete native helper session configuration.");
                        args.push_back(std::filesystem::absolute(session).wstring());
                    }
                    release();
                    const auto lease = randomToken();
                    auto output = [job](const std::string &text) {
                        job->channel->send({{"id", job->request.at("id")},
                                            {"event", "helper.stdout"},
                                            {"text", text}});
                    };
                    auto error = [job](const std::string &text) {
                        job->channel->send({{"id", job->request.at("id")},
                                            {"event", "helper.stderr"},
                                            {"text", text}});
                    };
                    auto helper = std::make_shared<BrokerProcess>(
                        paths.options.nativeDirectory / names.at(kind), args, output, error);
                    {
                        std::lock_guard lock(mutex);
                        helpers[lease] = {job->channel->owner, helper};
                        if (job->channel->disconnected)
                            helper->stop();
                    }
                    result(job, {{"code", 0}, {"passed", true}, {"lease", lease}});
                    int code = helper->wait();
                    {
                        std::lock_guard lock(mutex);
                        helpers.erase(lease);
                    }
                    job->channel->send(
                        {{"id", job->request.at("id")}, {"event", "helper.exit"}, {"code", code}});
                } else {
                    auto value = execute(
                        job->request,
                        [job](const std::string &text) {
                            job->channel->send({{"id", job->request.at("id")},
                                                {"event", "output"},
                                                {"text", text}});
                        },
                        &job->cancelled);
                    if (command == "inspect.close" && value.value("passed", false)) {
                        std::lock_guard lock(mutex);
                        job->channel->inspectionSessions.erase(
                            job->request.at("sessionId").get<std::string>());
                    }
                    if (value.contains("sessionId") && !job->externalSession.empty())
                        value["sessionId"] = job->externalSession;
                    if (job->cancelled) {
                        value["cancelled"] = true;
                        value["passed"] = false;
                        if (value.value("code", 0) == 0)
                            value["code"] = 130;
                    }
                    result(job, value);
                }
            } catch (const std::exception &error) {
                result(job,
                       {{"code", 2}, {"passed", false}, {"report", {{"error", error.what()}}}});
            }
            {
                std::lock_guard lock(mutex);
                jobs.erase(job->key);
                if (reservedHelper == job->key)
                    reservedHelper.clear();
            }
        }
    }
    void connection(std::shared_ptr<BrokerChannel> channel) {
        std::string pending, line;
        try {
            while (!stopped && !channel->disconnected &&
                   readLine(channel->pipe, pending, line, &stopped))
                dispatch(channel, json::parse(line));
            if (stopped && !channel->disconnected)
                readLine(channel->pipe, pending, line, nullptr, 2000);
        } catch (...) {
            channel->disconnected = true;
        }
        channel->disconnected = true;
        std::lock_guard lock(mutex);
        for (auto &[_, job] : jobs)
            if (job->channel == channel)
                job->cancelled = true;
        for (auto &[_, helper] : helpers)
            if (helper.first == channel->owner)
                helper.second->stop();
        if (!stopped && !channel->inspectionSessions.empty()) {
            auto cleanup = std::make_shared<BrokerJob>();
            cleanup->id = "cleanup";
            cleanup->key = channel->owner + ":cleanup";
            cleanup->request = {{"command", "inspect.close"}, {"id", "cleanup"}};
            cleanup->channel = channel;
            cleanup->cleanup = true;
            queue.push_back(cleanup);
            changed.notify_one();
        }
        --connectionCount;
        connectionsClosed.notify_all();
    }

    static int run(const BrokerOptions &options, BrokerRuntime::Executor execute,
                   BrokerRuntime::Release release) {
        auto state = std::make_shared<BrokerServer>();
        state->paths = prepare(options);
        state->execute = std::move(execute);
        state->release = std::move(release);
        PrivateSecurity security;
        // Kernel ownership contains every child process. A broker crash closes the
        // last job handle and terminates only this broker's native descendants.
        HANDLE processJob = CreateJobObjectW(nullptr, nullptr);
        if (!processJob)
            throw windowsError("Cannot establish broker process ownership");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(processJob, JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits)) ||
            !AssignProcessToJobObject(processJob, GetCurrentProcess())) {
            CloseHandle(processJob);
            throw windowsError("Cannot contain broker native children");
        }
        state->ownershipJob = processJob;
        std::ofstream(state->paths.owner) << json{
            {"brokerPID", GetCurrentProcessId()},
            {"nativePIDs", json::array()},
            {"sdkVersion",
             state->paths.options.version}}.dump();
        std::thread worker([state] { state->work(); });
        try {
            bool first = true;
            while (!state->stopped) {
                HANDLE pipe = CreateNamedPipeW(
                    state->paths.pipe.c_str(),
                    PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &security.attributes);
                first = false;
                if (pipe == INVALID_HANDLE_VALUE)
                    throw windowsError("Cannot create private native broker pipe");
                BOOL connected =
                    ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
                if (!connected || state->stopped) {
                    CloseHandle(pipe);
                    if (state->stopped)
                        break;
                    throw windowsError("Cannot accept native broker client");
                }
                auto channel = std::make_shared<BrokerChannel>(pipe);
                // A stalled consumer must not hold the global native worker or
                // state mutex indefinitely. Non-blocking byte writes above have a
                // bounded deadline; reads already wait with PeekNamedPipe.
                DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
                if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
                    throw windowsError("Cannot bound native broker pipe writes");
                {
                    std::lock_guard lock(state->mutex);
                    ++state->connectionCount;
                }
                std::thread([state, channel] { state->connection(channel); }).detach();
            }
        } catch (...) {
            state->stopped = true;
            {
                std::lock_guard lock(state->mutex);
                for (auto &[_, job] : state->jobs)
                    job->cancelled = true;
                for (auto &[_, helper] : state->helpers)
                    helper.second->stop();
            }
            state->changed.notify_all();
            worker.join();
            {
                std::unique_lock lock(state->mutex);
                state->connectionsClosed.wait(lock, [&] { return state->connectionCount == 0; });
            }
            state->release();
            throw;
        }
        state->changed.notify_all();
        worker.join();
        {
            std::unique_lock lock(state->mutex);
            state->connectionsClosed.wait(lock, [&] { return state->connectionCount == 0; });
        }
        state->release();
        std::ofstream(state->paths.owner) << json{
            {"brokerPID", 0},
            {"nativePIDs", json::array()},
            {"sdkVersion",
             state->paths.options.version}}.dump();
        // The OS closes processJob on process exit; explicitly closing it here
        // would terminate this process before main can return its exit status.
        return 0;
    }
};
} // namespace jade::native::broker
