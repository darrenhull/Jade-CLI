#pragma once
#include "BrokerSupport.hpp"
namespace jade::native::broker {
class BrokerClient {
  public:
    BrokerStatePaths paths;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string pending;
    std::mutex writes;
    explicit BrokerClient(const BrokerOptions &options)
        : paths(prepare(options)), pipe(ensureBroker(paths)) {}
    ~BrokerClient() {
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
    }
    void send(json request) {
        request["token"] = paths.secret;
        request["fingerprint"] = paths.options.fingerprint;
        std::lock_guard lock(writes);
        const auto text = request.dump() + "\n";
        if (text.size() > maximumPacket)
            throw std::runtime_error("Broker request exceeds 16 MiB limit.");
        writePipe(pipe, text);
    }
    bool receive(json &packet) {
        std::string line;
        if (!readLine(pipe, pending, line))
            return false;
        packet = json::parse(line);
        return true;
    }
    void verify() {
        send({{"id", "handshake"}, {"command", "status"}});
        json packet;
        if (!receive(packet))
            throw std::runtime_error("Native broker closed during handshake.");
        if (packet.at("result").value("fingerprint", "") != paths.options.fingerprint)
            throw std::runtime_error(
                "A different native Jade CLI build owns the broker. Finish its operations and run "
                "jade-cli shutdown before using this build.");
    }

    static json request(const BrokerOptions &options, const json &request,
                        BrokerRuntime::Output output) {
        BrokerClient client(options);
        const auto command = request.value("command", "");
        if (command != "status" && command != "shutdown")
            client.verify();
        auto packet = request;
        packet["id"] = "1";
        client.send(packet);
        json reply;
        while (client.receive(reply)) {
            if (reply.value("event", "") == "output" && output)
                output(reply.value("text", ""));
            if (reply.value("event", "") == "result")
                return reply.at("result");
        }
        throw std::runtime_error("Native broker closed without a result.");
    }
};
} // namespace jade::native::broker
