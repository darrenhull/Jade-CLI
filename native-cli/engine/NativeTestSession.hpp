#pragma once
#include "ProjectModel.hpp"
#include <memory>
namespace jade::native {
class NativeTestSession {
    std::unique_ptr<NativeProcess> process_;
    std::filesystem::path configFile_;
    std::string key_;
    int sequence_ = 0;

  public:
    ~NativeTestSession();
    void disconnect();
    json request(const std::filesystem::path &executable, const json &working,
                 const std::string &command, json args, NativeProcess::Output output,
                 std::atomic_bool *cancelled, unsigned timeout);
};
} // namespace jade::native
