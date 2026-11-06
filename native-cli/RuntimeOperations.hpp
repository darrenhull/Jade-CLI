#pragma once
#include "CommandLine.hpp"
#include "InspectorSession.hpp"
#include "engine/WorkingEngine.hpp"
#include <map>
#include <memory>
namespace jade::native {
class RuntimeOperations {
    std::filesystem::path root_, privateDirectory_;
    WorkingEngine working_;
    std::map<std::string, std::unique_ptr<InspectorSession>> inspections_;
    nlohmann::json runPrepared(const PreparedCommand &, const nlohmann::json &,
                               const WorkingEngine::Output &, std::atomic_bool *);
    void releaseInspections();

  public:
    RuntimeOperations(std::filesystem::path root, std::filesystem::path privateDirectory);
    nlohmann::json execute(const nlohmann::json &request, WorkingEngine::Output output = {},
                           std::atomic_bool *cancelled = nullptr);
    void disconnect();
};
} // namespace jade::native
