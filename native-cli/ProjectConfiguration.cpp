#include "ProjectConfiguration.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace jade::native {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;
std::string pathText(const fs::path &path) {
    return utf8(path.wstring());
}
fs::path pathValue(const fs::path &base, const json &value, const char *key) {
    if (!value.is_string() || value.get<std::string>().empty() ||
        value.get<std::string>().find('\0') != std::string::npos)
        throw std::runtime_error(std::string("Invalid configuration path: ") + key);
    return fs::absolute(base / fs::u8path(value.get<std::string>())).lexically_normal();
}
void normalize(json &config, const fs::path &base) {
    for (auto key : {"installPath", "databasePath", "iniPath", "storagePath", "program",
                     "workspace", "workingDatabaseDirectory"})
        if (config.contains(key)) {
            if (config[key].is_string() && config[key].get<std::string>().empty()) {
                config.erase(key);
                continue;
            }
            config[key] = pathText(pathValue(base, config[key], key));
        }
    for (auto key : {"workspaceRoots", "files"})
        if (config.contains(key)) {
            if (!config[key].is_array())
                throw std::runtime_error(std::string("Configuration paths must be an array: ") +
                                         key);
            for (auto &value : config[key])
                value = pathText(pathValue(base, value, key));
        }
}
std::string option(const json &options, const char *name) {
    return options.contains(name) ? options.at(name).get<std::string>() : std::string();
}
json existing(const fs::path &file) {
    auto value = json::parse(readText(file));
    if (!value.is_object())
        throw std::runtime_error("Configuration must be a JSON object.");
    return value;
}
void createProject(const fs::path &file, const json &config) {
    fs::create_directories(file.parent_path());
    auto temporary = file.parent_path() / (L".jade-init-" + wide(uniqueId()) + L".tmp");
    writeText(temporary, config.dump(2) + "\n");
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH)) {
        auto error = GetLastError();
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw std::runtime_error(
            error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS
                ? "jade.json was created by another process. Retry initialization."
                : "Cannot create Jade project configuration.");
    }
}
} // namespace
json ProjectConfiguration::resolve(const json &original, const fs::path &base,
                                   bool requireConnection) const {
    if (!original.is_object())
        throw std::runtime_error("Configuration must be a JSON object.");
    json project = original;
    normalize(project, base);
    json resolved = json::object();
    if (project.contains("connection")) {
        if (!project["connection"].is_string() || project["connection"].get<std::string>().empty())
            throw std::runtime_error("connection must name a Jade connection profile.");
        ConnectionProfiles profiles(profilesFile_);
        resolved = profiles.get(project["connection"]);
        normalize(resolved, profiles.file().parent_path());
    }
    resolved.update(project);
    if (!resolved.contains("workspaceRoots"))
        resolved["workspaceRoots"] = {pathText(fs::absolute(base).lexically_normal())};
    if (!resolved["workspaceRoots"].is_array() || resolved["workspaceRoots"].empty())
        throw std::runtime_error("Configure at least one workspace root.");
    if (!resolved.contains("storagePath")) {
        if (auto local = _wgetenv(L"LOCALAPPDATA"); local && *local)
            resolved["storagePath"] = pathText(fs::path(local) / L"JadeTools" / L"projects");
        else if (requireConnection)
            throw std::runtime_error("LOCALAPPDATA is required for private Jade project storage.");
    }
    if (!resolved.contains("installPath")) {
        if (requireConnection)
            throw std::runtime_error("Jade connection is not configured. Run jade-cli init "
                                     "--install <Jade folder> or choose --profile <name>.");
        return resolved;
    }
    if (!resolved.contains("databasePath"))
        resolved["databasePath"] =
            pathText(fs::u8path(resolved["installPath"].get<std::string>()) / L"system");
    if (!resolved.contains("iniPath"))
        resolved["iniPath"] =
            pathText(fs::u8path(resolved["databasePath"].get<std::string>()) / L"jade.ini");
    if (!resolved.contains("server"))
        resolved["server"] = "singleUser";
    if (resolved["server"] != "singleUser" && resolved["server"] != "multiUser")
        throw std::runtime_error("server must be singleUser or multiUser.");
    return resolved;
}
json ProjectConfiguration::initialize(const fs::path &projectFile, const fs::path &invocation,
                                      const json &options) const {
    auto file = fs::absolute(projectFile).lexically_normal();
    bool exists = fs::exists(file);
    auto previous = exists ? existing(file) : json::object();
    if (exists && option(options, "install").empty()) {
        json result = {{"created", false}, {"configFile", pathText(file)}};
        if (previous.contains("connection") && previous["connection"].is_string())
            result["connection"] = previous["connection"];
        return result;
    }
    ConnectionProfiles profiles(profilesFile_);
    auto name = option(options, "profile"), install = option(options, "install"),
         database = option(options, "database"), ini = option(options, "ini"),
         server = option(options, "server");
    if (name.empty() && previous.contains("connection") && previous["connection"].is_string())
        name = previous["connection"];
    auto available = profiles.names();
    if (install.empty() && !name.empty() &&
        std::find(available.begin(), available.end(), name) != available.end()) {
    } else if (install.empty() && name.empty() && available.size() == 1)
        name = available[0];
    else if (install.empty() && name.empty() && available.size() > 1) {
        if (!prompt_) {
            std::string names;
            for (auto &value : available)
                names += (names.empty() ? "" : ", ") + value;
            throw std::runtime_error(
                "Multiple Jade connection profiles are available (" + names +
                "). Choose --profile <name>; no interactive prompt is available.");
        }
        auto answer = prompt_("Choose a Jade connection profile", available);
        auto found = std::find(available.begin(), available.end(), answer);
        if (found != available.end())
            name = answer;
        else {
            size_t used = 0, index = 0;
            try {
                index = std::stoul(answer, &used);
            } catch (...) {
                throw std::runtime_error("Select a listed Jade connection profile.");
            }
            if (used != answer.size() || index == 0 || index > available.size())
                throw std::runtime_error("Select a listed Jade connection profile.");
            name = available[index - 1];
        }
    } else if (install.empty()) {
        if (!name.empty() && !prompt_)
            profiles.get(name);
        auto environment = _wgetenv(L"JADE_INSTALL");
        if (!environment || !*environment)
            environment = _wgetenv(L"JADE_HOME");
        if (environment && *environment)
            install = utf8(environment);
        else if (prompt_)
            install = prompt_("Jade installation folder", {});
        if (install.empty())
            throw std::runtime_error(
                "Jade connection setup is required. Run jade-cli init --install <Jade folder> or "
                "choose --profile <name>; noninteractive commands never prompt.");
        if (prompt_ && database.empty())
            database = prompt_("Source database folder (Enter for install/system)", {});
    }
    if (!install.empty()) {
        if (name.empty())
            name = "local";
        json settings = {{"installPath", pathText(pathValue(invocation, install, "installPath"))}};
        if (!database.empty())
            settings["databasePath"] = pathText(pathValue(invocation, database, "databasePath"));
        if (!ini.empty())
            settings["iniPath"] = pathText(pathValue(invocation, ini, "iniPath"));
        if (!server.empty())
            settings["server"] = server;
        profiles.save(name, settings);
    } else {
        if (!database.empty() || !ini.empty() || !server.empty())
            throw std::runtime_error(
                "Use --install when creating a connection with database/ini/server overrides.");
        profiles.get(name);
    }
    json project = {{"connection", name}};
    resolve(project, file.parent_path(), true);
    if (!exists)
        createProject(file, project);
    return {{"created", !exists}, {"configFile", pathText(file)}, {"connection", name}};
}
} // namespace jade::native
