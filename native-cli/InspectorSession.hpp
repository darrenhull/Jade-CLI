#pragma once
#include "engine/NativeProcess.hpp"
#include "json.hpp"
namespace jade::native {
class InspectorSession {
    NativeProcess process_;
    std::filesystem::path configuration_;
    size_t next_ = 0;

  public:
    const std::string identity;
    InspectorSession(const std::filesystem::path &executable,
                     const std::filesystem::path &privateDirectory, const nlohmann::json &config,
                     const std::string &identity);
    ~InspectorSession();
    nlohmann::json request(const std::string &command, const nlohmann::json &args,
                           std::atomic_bool *cancelled);
    void close();
};
} // namespace jade::native
