#include "RuntimeOperations.hpp"
#include "SourceCatalog.hpp"
#include "Version.hpp"
#include "ProjectConfiguration.hpp"
#include "engine/WorkspaceRunner.hpp"
#include <fstream>
#include <set>
namespace jade::native {
namespace fs = std::filesystem;
namespace {
std::string pathText(const fs::path &p) {
    return utf8(p.wstring());
}
std::vector<fs::path> roots(const nlohmann::json &config) {
    std::vector<fs::path> result;
    for (const auto &value :
         config.value("workspaceRoots", nlohmann::json::array({pathText(fs::current_path())})))
        result.emplace_back(wide(value));
    return result;
}
void validateSelection(const PreparedCommand &prepared) {
    for (const auto &file : prepared.files) {
        bool allowed = false;
        for (const auto &root : roots(prepared.config))
            if (contains(root, file)) {
                allowed = true;
                break;
            }
        if (!allowed)
            throw std::runtime_error(
                "Source files must remain within the configured workspace roots.");
    }
}
} // namespace
RuntimeOperations::RuntimeOperations(fs::path root, fs::path directory)
    : root_(std::move(root)), privateDirectory_(std::move(directory)), working_(root_) {}
void RuntimeOperations::releaseInspections() {
    std::string failure;
    for (auto it = inspections_.begin(); it != inspections_.end();) {
        try {
            it->second->close();
            it = inspections_.erase(it);
        } catch (const std::exception &error) {
            if (failure.empty())
                failure = error.what();
            ++it;
        }
    }
    if (!failure.empty())
        throw std::runtime_error(failure);
}
void RuntimeOperations::disconnect() {
    std::exception_ptr failure;
    try {
        releaseInspections();
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        working_.disconnect();
    } catch (...) {
        if (!failure)
            failure = std::current_exception();
    }
    if (failure)
        std::rethrow_exception(failure);
}
nlohmann::json RuntimeOperations::runPrepared(const PreparedCommand &prepared,
                                              const nlohmann::json &extras,
                                              const WorkingEngine::Output &output,
                                              std::atomic_bool *cancelled) {
    if (prepared.command == "init") {
        auto result = prepared.initialization;
        result["code"] = 0;
        result["passed"] = true;
        return result;
    }
    auto request = extras;
    request["command"] = prepared.command;
    request["config"] = prepared.config;
    if (extras.contains("methods"))
        request["config"]["methods"] = extras["methods"];
    for (const auto *key : {"query", "target", "limit", "offset"})
        if (prepared.values.contains(key))
            request[key] = prepared.values[key];
    if (prepared.values.contains("args"))
        request["args"] = nlohmann::json::parse(prepared.values["args"].get<std::string>());
    if (prepared.command == "workspace.run") {
        if (!prepared.files.empty()) {
            if (request.contains("sourceText"))
                throw std::runtime_error("Use a Workspace file or sourceText snapshot, not both.");
            const auto &file = prepared.files[0];
            if (!fs::is_regular_file(file) || fs::file_size(file) > 131074)
                throw std::runtime_error("Workspace file is missing or exceeds the source limit.");
            request["sourceText"] = readText(file);
            request["source"] = pathText(file);
        }
        return execute(request, output, cancelled);
    }
    if (prepared.command != "build" && prepared.command != "test")
        return execute(request, output, cancelled);
    auto selected = prepared.files;
    SourceCatalog catalog;
    if (prepared.projectBuild)
        selected = catalog.buildInputs(roots(prepared.config));
    if (selected.empty())
        throw std::runtime_error("No Jade source files found in the configured workspace.");
    nlohmann::json dependencies = prepared.config.value("files", nlohmann::json::array());
    if (prepared.command == "test")
        for (const auto &file : catalog.files(roots(prepared.config)))
            dependencies.push_back(pathText(file));
    for (const auto &file : selected)
        dependencies.push_back(pathText(file));
    std::set<std::string> seen;
    auto unique = nlohmann::json::array();
    for (const auto &file : dependencies)
        if (seen.insert(file).second)
            unique.push_back(file);
    request["config"]["files"] = unique;
    if (prepared.command == "build") {
        request["config"]["projectBuild"] = prepared.projectBuild;
        request["config"]["selectedFiles"] = nlohmann::json::array();
        for (const auto &file : selected)
            request["config"]["selectedFiles"].push_back(pathText(file));
        request["config"]["program"] = pathText(selected[0]);
        return execute(request, output, cancelled);
    }
    auto results = nlohmann::json::array();
    bool passed = true, wasCancelled = false;
    int code = 0;
    for (const auto &file : selected) {
        request["config"]["program"] = pathText(file);
        auto result = execute(request, output, cancelled);
        results.push_back({{"file", pathText(file)}, {"result", result}});
        if (!result.value("passed", false)) {
            passed = false;
            if (!code)
                code = result.value("code", 1);
        }
        if (result.value("cancelled", false)) {
            wasCancelled = true;
            break;
        }
    }
    if (selected.size() == 1)
        return results[0]["result"];
    return {{"code", code}, {"passed", passed}, {"cancelled", wasCancelled}, {"results", results}};
}
nlohmann::json RuntimeOperations::execute(const nlohmann::json &originalRequest,
                                          WorkingEngine::Output output,
                                          std::atomic_bool *cancelled) {
    auto request = originalRequest;
    const auto command = request.value("command", "");
    auto config = request.value("config", nlohmann::json::object());
    if (command != "cli" && command != "prepare" && command != "disconnect" &&
        command != "inspect.close") {
        const bool connectionRequired = command == "build" || command == "test" ||
                                        command == "workspace.run" ||
                                        command.starts_with("working.") ||
                                        command.starts_with("inspect.") || command == "native.run";
        config = ProjectConfiguration().resolve(
            config, fs::u8path(request.value("cwd", pathText(fs::current_path()))),
            connectionRequired);
        request["config"] = config;
    }
    if (command == "working.location")
        return {{"code", 0}, {"passed", true}, {"working", WorkingEngine::workingLocation(config)}};
    if (command == "working.prepare") {
        releaseInspections();
        return {{"code", 0},
                {"passed", true},
                {"working", working_.prepareDatabase(config, output, cancelled)}};
    }
    if (command == "inspect.target") {
        const auto target = request.value("target", "working");
        if (target != "source" && target != "working")
            throw std::runtime_error("Invalid database selection.");
        auto selected = target == "working" ? WorkingEngine::inspectionConfig(config) : config;
        selected["readOnly"] = true;
        return {{"code", 0}, {"passed", true}, {"oidFormat", "jade-native"}, {"config", selected}};
    }
    if (command == "cli" || command == "prepare") {
        const auto prepared = CommandLine().prepare(
            request.at("operation"), request.value("argv", std::vector<std::string>{}),
            fs::u8path(request.value("cwd", pathText(fs::current_path()))));
        if (request.value("restrictToWorkspace", false))
            validateSelection(prepared);
        if (command == "prepare")
            return {{"code", 0}, {"passed", true}, {"prepared", prepared.toJson()}};
        return runPrepared(prepared, request, output, cancelled);
    }
    if (command == "disconnect") {
        disconnect();
        return {{"code", 0}, {"passed", true}};
    }
    if (command == "doctor") {
        auto tools = nlohmann::json::object();
        const auto install = fs::u8path(config.value("installPath", ""));
        for (const auto *tool : {"jadclient", "jadloadb", "jdbutilb", "jdbadmin"})
            tools[tool] = !install.empty() &&
                          fs::is_regular_file(install / "bin" / (std::string(tool) + ".exe"));
        return {{"code", 0},
                {"passed", true},
                {"platform", "win32"},
                {"sdkVersion", cliVersion},
                {"runtimeImplementation", "native-cpp"},
                {"runtime", config.value("installPath", "")},
                {"tools", tools},
                {"capabilities",
                 {{"build", true},
                  {"test", true},
                  {"sourceSearch", true},
                  {"inspection", true},
                  {"oidFormat", "jade-native"},
                  {"nativeUtility", true},
                  {"workspace", true},
                  {"debug", false},
                  {"publishPlan", false}}}};
    }
    if (command == "source.search" || command == "tests.list") {
        auto number = [&](const char *key, size_t fallback) {
            if (!request.contains(key))
                return fallback;
            const auto &v = request[key];
            return v.is_string() ? size_t(std::max(0, std::stoi(v.get<std::string>())))
                                 : size_t(std::max(0, v.get<int>()));
        };
        return SourceCatalog().query(command, config, request.value("query", ""),
                                     number("offset", 0), number("limit", 100));
    }
    if (command == "build" || command == "test") {
        releaseInspections();
        return working_.execute(request, output, cancelled);
    }
    if (command == "workspace.run") {
        releaseInspections();
        return WorkspaceRunner(working_).execute(request, output, cancelled);
    }
    if (command == "native.run") {
        const auto install = fs::u8path(config.value("installPath", "")),
                   exe = fs::u8path(request.value("executable", ""));
        std::string name = exe.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (install.empty() ||
            !std::set<std::string>{"jadclient.exe", "jadloadb.exe", "jdbutilb.exe", "jdbadmin.exe"}
                 .contains(name) ||
            pathKey(exe) != pathKey(install / "bin" / name))
            throw std::runtime_error("Native executable is outside the Jade utility allowlist.");
        std::vector<std::wstring> args;
        for (const auto &a : request.at("args")) {
            auto value = a.get<std::string>();
            if (value.find('\0') != std::string::npos)
                throw std::runtime_error("Native arguments cannot contain NUL.");
            args.push_back(wide(value));
        }
        disconnect();
        NativeProcess process;
        const auto options = request.value("options", nlohmann::json::object());
        const auto cwd = options.contains("cwd") ? fs::u8path(options["cwd"].get<std::string>())
                                                 : exe.parent_path();
        const auto r = process.run(
            exe, args, cwd, cancelled,
            options.value("timeoutMilliseconds", config.value("timeoutMilliseconds", 120000u)),
            output);
        return {{"code", r.code},     {"success", r.success()},   {"passed", r.success()},
                {"output", r.output}, {"cancelled", r.cancelled}, {"timedOut", r.timedOut}};
    }
    if (command.starts_with("inspect.")) {
        const auto action = command.substr(8), sessionId = request.value("sessionId", "default"),
                   target = request.value("target", "working");
        if (target != "source" && target != "working")
            throw std::runtime_error("Invalid database selection.");
        if (action == "close") {
            auto it = inspections_.find(sessionId);
            if (it != inspections_.end()) {
                it->second->close();
                inspections_.erase(it);
            }
            return {{"code", 0}, {"passed", true}, {"body", {{"oidFormat", "jade-native"}}}};
        }
        if (!std::set<std::string>{"catalog", "instances", "object", "collection", "release"}
                 .contains(action))
            throw std::runtime_error("Unsupported inspection command.");
        auto selected = config;
        selected["readOnly"] = true;
        if (target == "working")
            selected = working_.inspectionConfig(config);
        selected["readOnly"] = true;
        auto binding =
            nlohmann::json::array(
                {target, selected.value("installPath", ""), selected.value("databasePath", ""),
                 selected.value("iniPath", ""), selected.value("server", "singleUser"),
                 selected.value("schema", ""), selected.value("user", "")})
                .dump();
        auto found = inspections_.find(sessionId);
        if (found != inspections_.end() && found->second->identity != binding)
            throw std::runtime_error("Inspection target changed. Close the existing session before "
                                     "connecting to another database.");
        working_.disconnect();
        if (found == inspections_.end()) {
            auto connected = std::make_unique<InspectorSession>(
                root_ / "native" / "jade-inspect-helper.exe", privateDirectory_ / "inspection",
                selected, binding);
            found = inspections_.emplace(sessionId, std::move(connected)).first;
        }
        return {{"code", 0},
                {"passed", true},
                {"sessionId", sessionId},
                {"body", found->second->request(
                             action, request.value("args", nlohmann::json::object()), cancelled)}};
    }
    throw std::runtime_error("Unsupported native operation: " + command);
}
} // namespace jade::native
