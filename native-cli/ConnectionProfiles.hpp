#pragma once
#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace jade::native {
class ConnectionProfiles {
    std::filesystem::path file_;
    nlohmann::json read() const;

  public:
    explicit ConnectionProfiles(std::filesystem::path file = {});
    static std::filesystem::path defaultFile();
    std::vector<std::string> names() const;
    nlohmann::json get(const std::string &name) const;
    void save(const std::string &name, const nlohmann::json &settings) const;
    const std::filesystem::path &file() const {
        return file_;
    }
};
} // namespace jade::native
