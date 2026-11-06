#pragma once
#include <filesystem>
#include <string>
namespace jade::native {
struct BrokerOptions {
    std::filesystem::path executable;
    std::filesystem::path nativeDirectory;
    std::filesystem::path stateDirectory;
    std::string fingerprint;
    std::string version = "0.2.0";
};
} // namespace jade::native
