#include "BrokerTestFixture.hpp"
#include "BrokerTestChild.hpp"
#include "BrokerTestPipe.hpp"
#include <fstream>
#include <future>
#include <sstream>
#include <iostream>
#include <functional>
#include <memory>

using namespace jade::native;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void removeFixture(const std::filesystem::path &root, const std::function<void()> &waitForExit) {
    require(std::filesystem::weakly_canonical(root.parent_path()) ==
                    std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) &&
                root.filename().wstring().starts_with(L"jade-broker-transport-"),
            "Refusing cleanup outside the owned TEMP fixture.");
    waitForExit();
    // The broker acknowledges shutdown before its worker/pipe threads exit and
    // owner.json is finally closed. Never delete state while that process lives.
    const auto deadline = GetTickCount64() + 3000;
    for (;;) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        if (!error)
            return;
        const bool transient = error == std::errc::directory_not_empty ||
                               error == std::errc::permission_denied ||
                               error == std::errc::device_or_resource_busy;
        if (!transient || GetTickCount64() >= deadline)
            throw std::filesystem::filesystem_error("Owned test fixture cleanup failed", root,
                                                    error);
        Sleep(20); // Only post-exit filesystem contention is retried.
    }
}
using OwnedProcess = std::unique_ptr<void, decltype(&CloseHandle)>;
OwnedProcess ownProcess(DWORD pid) {
    const auto handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!handle && GetLastError() != ERROR_INVALID_PARAMETER)
        throw std::runtime_error("Cannot observe the owned test broker's lifetime.");
    return OwnedProcess(handle, CloseHandle);
}
void waitForOwnedProcesses(const std::vector<OwnedProcess> &processes) {
    for (const auto &process : processes)
        if (process)
            require(WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0,
                    "The owned test broker did not exit; preserving its state directory.");
}
void verifyExitBeforeCleanup(const std::filesystem::path &executable) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("jade-broker-transport-cleanup-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    NativeProcess holder;
    try {
        holder.start(executable, {L"cleanup-holder"}, root);
        require(holder.line(nullptr, 5000, {}).starts_with("held"),
                "Cleanup holder did not acquire its file.");
        bool waited = false;
        removeFixture(root, [&] {
            waited = true;
            holder.write("close\n");
            require(holder.waitExit(5000), "The owned directory holder did not exit.");
        });
        require(waited && !std::filesystem::exists(root),
                "Fixture cleanup must wait for the owned process before removing its directory.");
    } catch (...) {
        holder.stop();
        std::error_code cleanup;
        std::filesystem::remove_all(root, cleanup);
        if (cleanup)
            std::cerr << "Cleanup regression evidence retained: " << cleanup.message() << '\n';
        throw;
    }
}
std::vector<nlohmann::json> packets(const std::string &text) {
    std::istringstream lines(text);
    std::string line;
    std::vector<nlohmann::json> result;
    while (std::getline(lines, line))
        result.push_back(nlohmann::json::parse(line));
    return result;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    BrokerOptions activeOptions;
    std::filesystem::path root, helperFile;
    std::vector<OwnedProcess> ownedBrokers;
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"cleanup-holder") {
            const auto file = CreateFileW(L"held-state", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
            require(file != INVALID_HANDLE_VALUE, "Cannot lock cleanup fixture.");
            std::cout << "held" << std::endl;
            std::string close;
            std::getline(std::cin, close);
            std::ofstream("late-owner.json") << "finished";
            CloseHandle(file);
            return 0;
        }
        if (auto handled = BrokerTestFixture::handle(argc, argv))
            return *handled;
        auto options = BrokerTestFixture::options();
        verifyExitBeforeCleanup(options.executable);
        root = std::filesystem::temp_directory_path() /
               ("jade-broker-transport-" + std::to_string(GetCurrentProcessId()));
        options.stateDirectory = root;
        activeOptions = options;
        const auto first = BrokerRuntime::request(options, {{"command", "status"}});
        const auto originalPid = first.at("brokerPID").get<DWORD>();
        ownedBrokers.push_back(ownProcess(originalPid));
        std::ifstream tokenFile(root / L"token");
        std::string token;
        tokenFile >> token;
        tokenFile.close();
        {
            BrokerTestPipe unauthenticated(root);
            require(unauthenticated.write(nlohmann::json{{"id", "bad"},
                                                         {"command", "status"},
                                                         {"token", "incorrect"},
                                                         {"fingerprint", "test-artifact"}}
                                              .dump() +
                                          "\n"),
                    "Could not send authentication fixture.");
            require(unauthenticated.closedWithin(1000),
                    "Invalid broker token did not close the connection.");
        }
        {
            BrokerTestPipe malformedPipe(root);
            require(malformedPipe.write("]\n"), "Could not send malformed fixture.");
            require(malformedPipe.closedWithin(1000),
                    "Malformed pipe JSON did not close the connection.");
        }
        {
            BrokerTestPipe oversizedPipe(root);
            oversizedPipe.write(std::string(16 * 1024 * 1024 + 8192, 'x'));
            require(oversizedPipe.closedWithin(3000),
                    "Oversized raw pipe packet was not rejected.");
        }
        {
            BrokerTestPipe fragmented(root);
            auto packet = nlohmann::json{{"id", "split"},
                                         {"command", "status"},
                                         {"token", token},
                                         {"fingerprint", "test-artifact"}}
                              .dump() +
                          "\n";
            require(fragmented.write(packet.substr(0, 13)), "Cannot send initial packet fragment.");
            Sleep(20);
            require(fragmented.write(packet.substr(13) + packet),
                    "Cannot send coalesced packet fragments.");
            require(nlohmann::json::parse(fragmented.line()).at("result").at("passed") == true,
                    "Fragmented request failed.");
            require(nlohmann::json::parse(fragmented.line()).at("result").at("passed") == true,
                    "Coalesced request was lost.");
        }
        std::string malformed = "]\n{\"id\":\"good\",\"command\":\"echo\"}\n";
        std::istringstream input(malformed);
        std::ostringstream output;
        require(BrokerRuntime::service(options, input, output) == 0,
                "Malformed request broke subsequent service calls.");
        bool malformedFailed = false, subsequentPassed = false;
        for (auto &packet : packets(output.str()))
            if (packet.value("event", "") == "result") {
                if (packet["id"].is_null())
                    malformedFailed = packet["result"].value("code", 0) == 2;
                else if (packet["id"] == "good")
                    subsequentPassed = packet["result"].value("passed", false);
            }
        require(malformedFailed && subsequentPassed,
                "Malformed JSON must produce an error and allow recovery.");
        std::istringstream oversized(std::string(16 * 1024 * 1024 + 1, 'x') + "\n");
        std::ostringstream oversizedOutput;
        require(BrokerRuntime::service(options, oversized, oversizedOutput) == 0,
                "Oversized request recovery failed.");
        require(packets(oversizedOutput.str()).at(0).at("result").at("code") == 2,
                "Oversized service request was accepted.");
        std::ostringstream queued;
        for (int i = 0; i < 40; ++i)
            queued << nlohmann::json{{"id", i}, {"command", "wait"}, {"duration", 100}}.dump()
                   << '\n';
        std::istringstream queuedInput(queued.str());
        std::ostringstream queuedOutput;
        require(BrokerRuntime::service(options, queuedInput, queuedOutput) == 0,
                "Queue service failed.");
        unsigned busy = 0, completed = 0;
        for (auto &p : packets(queuedOutput.str()))
            if (p.value("event", "") == "result") {
                ++completed;
                if (p["result"].value("busy", false))
                    ++busy;
            }
        require(completed == 40 && busy > 0, "Broker queue did not enforce its per-client bound.");
        {
            BrokerTestChild abandoned(options.executable,
                                      {L"client", L"--state-dir", root.wstring()});
            abandoned.write("{\"id\":\"abandoned\",\"command\":\"wait\",\"duration\":10000}\n");
            require(abandoned.waitForOutput(5000), "Abandoned operation did not start.");
            const auto beginning = GetTickCount64();
            auto other = std::async(std::launch::async, [&] {
                return BrokerRuntime::request(options, {{"command", "echo"}});
            });
            Sleep(30);
            abandoned.stop();
            require(other.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                    "Disconnected client did not release its native operation.");
            require(other.get().at("passed") == true,
                    "Disconnect cancelled another client's operation.");
            require(GetTickCount64() - beginning < 3000, "Disconnect cancellation was too slow.");
        }
        {
            BrokerTestChild stalled(options.executable,
                                    {L"client", L"--state-dir", root.wstring()});
            stalled.write("{\"id\":\"flood\",\"command\":\"flood\"}\n");
            require(stalled.waitForOutput(5000), "Slow-reader fixture did not start.");
            Sleep(50);
            auto next = std::async(std::launch::async, [&] {
                return BrokerRuntime::request(options, {{"command", "echo"}});
            });
            const bool recovered =
                next.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
            stalled.stop();
            require(next.get().at("passed") == true,
                    "Slow reader damaged another client's result.");
            require(recovered, "A client that stopped reading progress blocked the entire broker.");
        }
        const auto helperDir = options.executable.parent_path() / L"native";
        helperFile = helperDir / L"jade-inspect-helper.exe";
        std::filesystem::create_directories(helperDir);
        std::filesystem::copy_file(options.executable, helperDir / L"jade-inspect-helper.exe",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto session = root / L"session.json";
        std::ofstream(session)
            << nlohmann::json{{"installPath", "fixture"}, {"databasePath", "fixture"}}.dump();
        {
            std::istringstream split("split\n");
            std::ostringstream splitOutput, errors;
            require(BrokerRuntime::relayHelper(options, "inspect", session, split, splitOutput,
                                               errors) == 0,
                    "Split UTF-8 helper failed.");
            require(splitOutput.str() == "\xf0\x9f\x90\x9f\r\n",
                    "Split UTF-8 bytes were corrupted.");
        }
        {
            BrokerTestChild relay(options.executable, {L"relay", L"--state-dir", root.wstring(),
                                                       L"--session", session.wstring()});
            relay.write("exit42\n");
            require(relay.wait(3000), "Helper exit left the CLI blocked reading stdin.");
            require(relay.code() == 42, "Helper exit code was lost.");
        }
        bool crashReported = false;
        try {
            BrokerRuntime::request(options, {{"command", "crash"}});
        } catch (const std::exception &) {
            crashReported = true;
        }
        require(crashReported, "Broker crash was reported as success.");
        const auto restarted = BrokerRuntime::request(options, {{"command", "status"}});
        ownedBrokers.push_back(ownProcess(restarted.at("brokerPID").get<DWORD>()));
        require(restarted.at("brokerPID").get<DWORD>() != originalPid,
                "Crashed broker was not replaced.");
        require(BrokerRuntime::request(options, {{"command", "shutdown"}}).at("passed") == true,
                "Restarted broker shutdown failed.");
        const auto badRoot = root / L"corrupt-token";
        std::filesystem::create_directories(badRoot);
        std::ofstream(badRoot / L"token") << "corrupt";
        auto bad = options;
        bad.stateDirectory = badRoot;
        bool refused = false;
        try {
            BrokerRuntime::request(bad, {{"command", "status"}});
        } catch (const std::exception &) {
            refused = true;
        }
        require(refused, "Corrupt token was silently replaced.");
        const auto staleRoot = root / L"stale-owner";
        std::filesystem::create_directories(staleRoot);
        std::ofstream(staleRoot / L"owner.json")
            << nlohmann::json{{"brokerPID", 0},
                              {"nativePIDs", nlohmann::json::array({GetCurrentProcessId()})}}
                   .dump();
        auto stale = options;
        stale.stateDirectory = staleRoot;
        refused = false;
        try {
            BrokerRuntime::request(stale, {{"command", "status"}});
        } catch (const std::exception &) {
            refused = true;
        }
        require(refused, "Surviving native owner did not prevent broker restart.");
        removeFixture(root, [&] { waitForOwnedProcesses(ownedBrokers); });
        std::filesystem::remove(helperFile);
        std::cout << "Broker transport malformed JSON, bounds, disconnect, helper UTF-8/exit, "
                     "crash recovery and private state passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        if (!activeOptions.stateDirectory.empty() && !ownedBrokers.empty())
            try {
                const auto &last = ownedBrokers.back();
                if (last && WaitForSingleObject(last.get(), 0) == WAIT_TIMEOUT)
                    BrokerRuntime::request(activeOptions, {{"command", "shutdown"}});
                removeFixture(root, [&] { waitForOwnedProcesses(ownedBrokers); });
                if (!helperFile.empty())
                    std::filesystem::remove(helperFile);
            } catch (const std::exception &cleanup) {
                std::cerr << "Cleanup failed; preserving the original failure and owned state: "
                          << cleanup.what() << std::endl;
            }
        return 1;
    }
}
