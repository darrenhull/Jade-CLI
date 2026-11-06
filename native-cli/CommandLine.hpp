#pragma once
#include "json.hpp"
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
    nlohmann::json toJson() const;
};
class CommandLine {
  public:
    PreparedCommand prepare(
        const std::string &command, const std::vector<std::string> &argv,
        const std::filesystem::path &cwd = std::filesystem::current_path()) const;
};
} // namespace jade::native
