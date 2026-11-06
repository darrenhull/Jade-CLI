#pragma once
#include "json.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace jade::native {
class ProjectConfiguration {
  public:
    using Prompt =
        std::function<std::string(const std::string &, const std::vector<std::string> &)>;

  private:
    std::filesystem::path profilesFile_;
    Prompt prompt_;

  public:
    explicit ProjectConfiguration(std::filesystem::path profilesFile = {}, Prompt prompt = {})
        : profilesFile_(std::move(profilesFile)), prompt_(std::move(prompt)) {}
    nlohmann::json resolve(const nlohmann::json &config, const std::filesystem::path &base,
                           bool requireConnection = false) const;
    nlohmann::json initialize(const std::filesystem::path &projectFile,
                              const std::filesystem::path &invocation,
                              const nlohmann::json &options = nlohmann::json::object()) const;
};
} // namespace jade::native
