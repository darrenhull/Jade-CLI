#pragma once
#include "json.hpp"
#include <filesystem>
#include <vector>
#include <string>
namespace jade::native {
class SourceCatalog {
  public:
    std::vector<std::filesystem::path> files(const std::vector<std::filesystem::path> &roots) const;
    std::vector<std::filesystem::path>
    buildInputs(const std::vector<std::filesystem::path> &roots) const;
    nlohmann::json query(const std::string &command, const nlohmann::json &config,
                         const std::string &query = "", size_t offset = 0,
                         size_t limit = 100) const;
};
} // namespace jade::native
