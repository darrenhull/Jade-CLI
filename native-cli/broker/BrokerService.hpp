#pragma once
#include "BrokerClient.hpp"
namespace jade::native::broker {
class BrokerService {
  public:
    static int run(const BrokerOptions &options, std::istream &input, std::ostream &output) {
        BrokerClient client(options);
        client.verify();
        std::mutex stateMutex, outputMutex;
        std::condition_variable finished;
        std::map<std::string, json> ids;
        std::map<std::string, std::string> active;
        std::atomic_bool closed = false, failed = false;
        unsigned sequence = 0;
        std::thread reader([&] {
            std::string reason = "Native broker connection closed before completing the request.";
            try {
                json packet;
                while (client.receive(packet)) {
                    json original;
                    bool found = false;
                    {
                        std::lock_guard lock(stateMutex);
                        auto it = ids.find(packet.value("id", ""));
                        if (it != ids.end()) {
                            original = it->second;
                            found = true;
                            if (packet.value("event", "") == "result") {
                                active.erase(original.dump());
                                ids.erase(it);
                                finished.notify_all();
                            }
                        }
                    }
                    if (found) {
                        packet["id"] = original;
                        std::lock_guard lock(outputMutex);
                        output << packet.dump() << '\n';
                        output.flush();
                    }
                }
            } catch (const std::exception &error) {
                reason = error.what();
            }
            {
                std::lock_guard stateLock(stateMutex);
                std::lock_guard outputLock(outputMutex);
                if (!ids.empty())
                    failed = true;
                for (const auto &[_, id] : ids)
                    output
                        << json{{"id", id},
                                {"event", "result"},
                                {"result",
                                 {{"code", 2}, {"passed", false}, {"report", {{"error", reason}}}}}}
                               .dump()
                        << '\n';
                output.flush();
                ids.clear();
            }
            closed = true;
            finished.notify_all();
        });
        try {
            std::string line;
            while (std::getline(input, line)) {
                json request;
                try {
                    if (line.size() > maximumPacket)
                        throw std::runtime_error("Service request exceeds 16 MiB limit.");
                    request = json::parse(line);
                    if (!request.contains("id") ||
                        (!request["id"].is_string() && !request["id"].is_number_integer()))
                        throw std::runtime_error("Service request id required.");
                } catch (const std::exception &error) {
                    std::lock_guard lock(outputMutex);
                    output << json{{"id", nullptr},
                                   {"event", "result"},
                                   {"result",
                                    {{"code", 2},
                                     {"passed", false},
                                     {"report", {{"error", error.what()}}}}}}
                                  .dump()
                           << '\n';
                    output.flush();
                    continue;
                }
                {
                    std::lock_guard lock(stateMutex);
                    const auto id = std::to_string(++sequence);
                    ids[id] = request["id"];
                    if (request.value("command", "") == "cancel") {
                        const auto key = request.value("operationId", json()).dump();
                        auto found = active.find(key);
                        if (found != active.end())
                            request["operationId"] = found->second;
                    } else
                        active[request["id"].dump()] = id;
                    request["id"] = id;
                }
                client.send(request);
            }
            // EOF means no more input, not cancellation: drain submitted requests.
            {
                std::unique_lock lock(stateMutex);
                finished.wait(lock, [&] { return ids.empty() || closed; });
            }
        } catch (...) {
            CancelIoEx(client.pipe, nullptr);
            CloseHandle(client.pipe);
            client.pipe = INVALID_HANDLE_VALUE;
            if (reader.joinable())
                reader.join();
            throw;
        }
        CancelIoEx(client.pipe, nullptr);
        CloseHandle(client.pipe);
        client.pipe = INVALID_HANDLE_VALUE;
        if (reader.joinable())
            reader.join();
        return failed ? 2 : 0;
    }
};
} // namespace jade::native::broker
