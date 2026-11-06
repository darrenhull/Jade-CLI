#pragma once
#include "BrokerSupport.hpp"
namespace jade::native::broker {
class BrokerChannel {
  public:
    HANDLE pipe;
    std::string owner;
    std::mutex writes;
    std::atomic_bool disconnected = false;
    std::set<std::string> inspectionSessions;
    explicit BrokerChannel(HANDLE value) : pipe(value), owner(randomToken()) {}
    ~BrokerChannel() {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    void send(const json &packet) {
        if (disconnected)
            return;
        try {
            const auto text = packet.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
            if (text.size() > maximumPacket)
                throw std::runtime_error("Broker response exceeds packet limit.");
            std::lock_guard lock(writes);
            writePipe(pipe, text);
        } catch (...) {
            disconnected = true;
        }
    }
};
} // namespace jade::native::broker
