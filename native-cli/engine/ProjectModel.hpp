#pragma once
#include "SourceModel.hpp"
#include "NativeProcess.hpp"
#include <filesystem>
#include <map>
#include <set>
#include "json.hpp"
namespace jade::native {
using json = nlohmann::json;
std::string readText(const std::filesystem::path &file);
void writeText(const std::filesystem::path &file, const std::string &text);
std::string sha256(const std::string &data);
std::string uniqueId();
std::string pathKey(const std::filesystem::path &file);
bool contains(const std::filesystem::path &root, const std::filesystem::path &child);
struct SourceEntry {
    std::filesystem::path file;
    std::string extension, text, hash, shape;
    SourceModel model;
};
void stageSourceFile(const SourceEntry &source, const std::filesystem::path &target);
struct ProjectSchema {
    std::string name, parent;
    std::filesystem::path folder, directory, joined, ddx;
    SourceEntry header;
    std::vector<SourceEntry> resources, classes;
};
class ProjectModel {
  public:
    std::vector<ProjectSchema> schemas;
    std::vector<SourceEntry> loose, migrations;
    std::filesystem::path stageDirectory;
    static std::vector<std::filesystem::path> listFiles(const std::filesystem::path &root);
    static ProjectModel scan(const std::vector<std::filesystem::path> &roots,
                             const std::vector<std::filesystem::path> &explicitFiles = {});
    ProjectModel dependencies(const std::filesystem::path &program) const;
    ProjectModel select(const std::vector<std::filesystem::path> &files) const;
    json inventory() const;
    void stage(const std::filesystem::path &destination);
};
} // namespace jade::native
