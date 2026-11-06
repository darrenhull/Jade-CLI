#include "CommandLine.hpp"
#include <fstream>
#include <set>
#include <stdexcept>
#include <windows.h>
namespace jade::native {
namespace fs = std::filesystem;
namespace {
fs::path absolute(const fs::path &root, const std::string &value) {
    if (value.empty() || value.find('\0') != std::string::npos)
        throw std::runtime_error("A path must be a nonempty string without NUL characters.");
    return fs::absolute(root / fs::u8path(value)).lexically_normal();
}
std::string text(const fs::path &path) {
    const auto u = path.u8string();
    return {u.begin(), u.end()};
}
std::wstring identity(const fs::path &path) {
    const auto canonical = fs::weakly_canonical(path).wstring();
    std::wstring result(canonical.size(), 0);
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, canonical.data(),
                       int(canonical.size()), result.data(), int(result.size()), nullptr, nullptr,
                       0))
        throw std::runtime_error("Cannot normalize a Windows source path.");
    return result;
}
} // namespace
nlohmann::json PreparedCommand::toJson() const {
    nlohmann::json paths = nlohmann::json::array();
    for (const auto &f : files)
        paths.push_back(text(f));
    return {{"command", command},
            {"values", values},
            {"config", config},
            {"configFile",
             configFile.empty() ? nlohmann::json(nullptr) : nlohmann::json(text(configFile))},
            {"files", paths},
            {"projectBuild", projectBuild}};
}
PreparedCommand CommandLine::prepare(const std::string &command,
                                     const std::vector<std::string> &argv,
                                     const fs::path &cwd) const {
    PreparedCommand result;
    result.command = command;
    bool positional = false;
    std::vector<fs::path> files;
    const std::set<std::string> options = {"config", "workspace", "program", "query",
                                           "limit",  "offset",    "target",  "args"};
    for (size_t i = 0; i < argv.size(); ++i) {
        const auto &arg = argv[i];
        if (!positional && arg == "--") {
            positional = true;
            continue;
        }
        if (!positional && arg.starts_with("--")) {
            const auto key = arg.substr(2);
            if (key == "json") {
                result.values[key] = true;
                continue;
            }
            if (!options.contains(key))
                throw std::runtime_error("Unknown option: " + arg);
            if (i + 1 == argv.size() || argv[i + 1].starts_with("--"))
                throw std::runtime_error("Missing value for " + arg);
            result.values[key] = argv[++i];
        } else
            files.push_back(absolute(cwd, arg));
    }
    if (result.values.contains("config"))
        result.configFile = absolute(cwd, result.values["config"]);
    else
        for (auto directory = fs::absolute(cwd);; directory = directory.parent_path()) {
            if (fs::is_regular_file(directory / "jade.json")) {
                result.configFile = directory / "jade.json";
                break;
            }
            if (directory == directory.parent_path())
                break;
        }
    if (result.configFile.empty() && (command == "build" || command == "test"))
        throw std::runtime_error("No jade.json found in this folder or its parents. Create "
                                 "jade.json or supply --config <file>.");
    if (!result.configFile.empty()) {
        std::ifstream in(result.configFile, std::ios::binary);
        if (!in)
            throw std::runtime_error("Cannot read configuration file.");
        in >> result.config;
        if (!result.config.is_object())
            throw std::runtime_error("Configuration must be a JSON object.");
    }
    const auto base = result.configFile.empty() ? cwd : result.configFile.parent_path();
    for (const auto *key : {"installPath", "databasePath", "iniPath", "storagePath", "program",
                            "workspace", "workingDatabaseDirectory"})
        if (result.config.contains(key)) {
            if (!result.config[key].is_string())
                throw std::runtime_error(std::string("Configuration path must be a string: ") +
                                         key);
            if (result.config[key].get_ref<const std::string &>().empty()) {
                result.config.erase(key);
                continue;
            }
            result.config[key] = text(absolute(base, result.config[key]));
        }
    for (const auto *key : {"workspaceRoots", "files"})
        if (result.config.contains(key)) {
            if (!result.config[key].is_array())
                throw std::runtime_error(std::string("Configuration paths must be an array: ") +
                                         key);
            for (auto &value : result.config[key])
                value = text(absolute(base, value));
        }
    if (!result.config.contains("workspaceRoots"))
        result.config["workspaceRoots"] = {text(fs::absolute(base))};
    if (result.config["workspaceRoots"].empty())
        throw std::runtime_error("Configure at least one workspace root.");
    if (result.values.contains("workspace"))
        result.config["workspaceRoots"] = {text(absolute(cwd, result.values["workspace"]))};
    if (result.values.contains("program")) {
        if (!files.empty())
            throw std::runtime_error("Use positional source files or --program, not both.");
        files.push_back(absolute(cwd, result.values["program"]));
    }
    if (!files.empty() && command != "build" && command != "test")
        throw std::runtime_error("Source file arguments are supported by build and test.");
    result.projectBuild = command == "build" && files.empty();
    if (files.empty() && !result.projectBuild && result.config.contains("program"))
        files.push_back(fs::u8path(result.config["program"].get<std::string>()));
    std::set<std::wstring> seen;
    for (const auto &file : files)
        if (seen.insert(identity(file)).second)
            result.files.push_back(file);
    if (command == "build" || command == "test") {
        if (result.files.empty() && !result.projectBuild)
            throw std::runtime_error("Supply at least one source file to " + command + '.');
        for (const auto &file : result.files)
            if (!fs::is_regular_file(file))
                throw std::runtime_error("Source is not a file: " + text(file));
    }
    return result;
}
} // namespace jade::native
