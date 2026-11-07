#pragma once
#include "NativeTestSession.hpp"
namespace jade::native {
class WorkingEngine {
    std::filesystem::path distributionRoot_;
    NativeTestSession session_;
    json prepare(const json &config, NativeProcess::Output output, std::atomic_bool *cancelled);
    json load(const json &working, const std::vector<std::wstring> &args,
              NativeProcess::Output output, std::atomic_bool *cancelled);
    json executeImpl(const json &request, ProjectModel *project, bool completeScope,
                     NativeProcess::Output output, std::atomic_bool *cancelled);

  public:
    using Output = NativeProcess::Output;
    explicit WorkingEngine(std::filesystem::path distributionRoot)
        : distributionRoot_(std::move(distributionRoot)) {}
    static json workingLocation(const json &config);
    static json inspectionConfig(const json &config);
    json prepareDatabase(const json &config, Output output = {},
                         std::atomic_bool *cancelled = nullptr) {
        return prepare(config, output, cancelled);
    }
    static std::string isolatedIni(const std::string &text, const std::filesystem::path &root);
    json execute(const json &request, Output output = {}, std::atomic_bool *cancelled = nullptr);
    json executeProject(const json &request, ProjectModel project, bool completeScope,
                        Output output = {}, std::atomic_bool *cancelled = nullptr);
    void disconnect() {
        session_.disconnect();
    }
};
} // namespace jade::native
