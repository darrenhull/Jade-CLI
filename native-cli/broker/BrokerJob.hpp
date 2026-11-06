#pragma once
#include "BrokerChannel.hpp"
namespace jade::native::broker {
struct BrokerJob {
    std::string id, key, externalSession;
    json request;
    std::shared_ptr<BrokerChannel> channel;
    std::atomic_bool cancelled = false;
    std::string state = "queued";
    bool cleanup = false;
};
} // namespace jade::native::broker
