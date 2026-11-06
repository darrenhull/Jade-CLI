#pragma once
#include "json.hpp"
#include "ProjectConfiguration.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace jade::native {
struct PreparedCommand {
    std::string command;
    nlohmann::json values = nlohmann::json::object(), config = nlohmann::json::object();
    std::filesystem::path configFile;
    std::vector<std::filesystem::path> files;
    bool projectBuild = false;
    nlohmann::json initialization = nlohmann::json::object();
    nlohmann::json toJson() const;
};
class CommandLine {
    ProjectConfiguration configuration_;

  public:
    explicit CommandLine(std::filesystem::path profilesFile = {},
                         ProjectConfiguration::Prompt prompt = {})
        : configuration_(std::move(profilesFile), std::move(prompt)) {}
    PreparedCommand prepare(
        const std::string &command, const std::vector<std::string> &argv,
        const std::filesystem::path &cwd = std::filesystem::current_path()) const;
};
} // namespace jade::native
