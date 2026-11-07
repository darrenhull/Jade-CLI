#pragma once
#include "WorkingEngine.hpp"
namespace jade::native {
/** Executes signature-free Jade code using Jade's transient Workspace compiler. */
class WorkspaceRunner {
    WorkingEngine &working_;

  public:
    explicit WorkspaceRunner(WorkingEngine &working) : working_(working) {}
    static void validate(const json &request);
    static std::string launcher(const std::string &source, const std::string &marker);
    static json result(const ProcessResult &process, const std::string &marker, const json &request,
                       const std::string &database, double elapsed,
                       const std::string &exceptionLog = {});
    json execute(const json &request, NativeProcess::Output output = {},
                 std::atomic_bool *cancelled = nullptr);
};
} // namespace jade::native
