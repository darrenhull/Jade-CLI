#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
class DdxMerger {
  public:
    static std::string merge(const std::string &schema,
                             const std::vector<std::filesystem::path> &files);
};
} // namespace jade::native
