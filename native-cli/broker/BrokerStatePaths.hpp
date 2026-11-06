#pragma once
#include "BrokerRuntime.hpp"
namespace jade::native::broker {
struct BrokerStatePaths {
    BrokerOptions options;
    std::filesystem::path root, token, owner;
    std::wstring pipe, mutex;
    std::string secret;
};
} // namespace jade::native::broker
