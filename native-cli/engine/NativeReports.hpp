#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
class NativeReports {
  public:
    static json tests(const std::string &log, const SourceModel &model,
                      const std::vector<std::string> &methods);
    static json diagnostics(const std::string &output, const std::vector<SourceEntry> &entries);
};
} // namespace jade::native
