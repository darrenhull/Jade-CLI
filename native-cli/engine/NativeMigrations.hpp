#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
struct NativeRename {
    std::string file, version, schema, oldName, newName;
};
class NativeMigrations {
  public:
    static std::vector<NativeRename> parse(const std::string &text, const std::string &file);
    static std::vector<NativeRename> plan(const std::vector<NativeRename> &operations,
                                          const std::set<std::string> &installed,
                                          const std::set<std::string> &source);
    static void apply(const ProjectModel &project, const json &working,
                      const std::filesystem::path &distributionRoot, NativeProcess::Output output,
                      std::atomic_bool *cancelled);
};
} // namespace jade::native
