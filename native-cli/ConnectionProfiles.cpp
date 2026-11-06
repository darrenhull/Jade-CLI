#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include "engine/PrivateFile.hpp"
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <memory>
namespace jade::native {
namespace {
void validateName(const std::string &name) {
    if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }))
        throw std::runtime_error(
            "Connection profile name must be a nonempty name of at most128 bytes.");
}
void validateSettings(const nlohmann::json &settings) {
    if (!settings.is_object() || !settings.contains("installPath") ||
        !settings["installPath"].is_string() || settings["installPath"].get<std::string>().empty())
        throw std::runtime_error("A connection profile needs installPath.");
    for (auto key : {"installPath", "databasePath", "iniPath"})
        if (settings.contains(key) &&
            (!settings[key].is_string() ||
             settings[key].get<std::string>().find('\0') != std::string::npos))
            throw std::runtime_error(std::string("Invalid connection path: ") + key);
    if (settings.contains("server") && settings["server"] != "singleUser" &&
        settings["server"] != "multiUser")
        throw std::runtime_error("Connection server must be singleUser or multiUser.");
}
} // namespace
ConnectionProfiles::ConnectionProfiles(std::filesystem::path file)
    : file_(file.empty() ? defaultFile() : std::move(file)) {
    file_ = std::filesystem::absolute(file_).lexically_normal();
}
std::filesystem::path ConnectionProfiles::defaultFile() {
    if (auto value = _wgetenv(L"JADE_TOOLS_CONNECTIONS_FILE"); value && *value) {
        auto path = std::filesystem::path(value);
        if (!path.is_absolute())
            throw std::runtime_error("JADE_TOOLS_CONNECTIONS_FILE must be an absolute path.");
        return path;
    }
    if (auto value = _wgetenv(L"LOCALAPPDATA"); value && *value)
        return std::filesystem::path(value) / L"JadeTools" / L"connections.json";
    throw std::runtime_error("LOCALAPPDATA is required for private Jade connection profiles.");
}
nlohmann::json ConnectionProfiles::read() const {
    if (!std::filesystem::exists(file_))
        return {{"formatVersion", 1}, {"profiles", nlohmann::json::object()}};
    if (!std::filesystem::is_regular_file(file_) || std::filesystem::file_size(file_) > 1024 * 1024)
        throw std::runtime_error("Invalid Jade connection profile file.");
    auto value = nlohmann::json::parse(readText(file_));
    if (!value.is_object() || value.value("formatVersion", 0) != 1 || !value.contains("profiles") ||
        !value["profiles"].is_object())
        throw std::runtime_error("Unsupported Jade connection profile format.");
    return value;
}
std::vector<std::string> ConnectionProfiles::names() const {
    std::vector<std::string> result;
    auto values = read();
    for (auto &[name, settings] : values.at("profiles").items()) {
        validateName(name);
        validateSettings(settings);
        result.push_back(name);
    }
    return result;
}
nlohmann::json ConnectionProfiles::get(const std::string &name) const {
    validateName(name);
    auto values = read();
    if (!values["profiles"].contains(name))
        throw std::runtime_error("Unknown Jade connection profile '" + name +
                                 "'. Run jade-cli init --profile \"" + name +
                                 "\" --install <Jade folder>.");
    auto result = values["profiles"][name];
    validateSettings(result);
    return result;
}
void ConnectionProfiles::save(const std::string &name, const nlohmann::json &settings) const {
    validateName(name);
    validateSettings(settings);
    auto parent = file_.parent_path();
    std::filesystem::create_directories(parent);
    if (GetFileAttributesW(parent.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Private connection profile folder cannot be a reparse point.");
    PrivateFile::protectDirectory(parent);
    const auto lockPath = parent / (file_.filename().wstring() + L".lock");
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        handle =
            CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_SHARING_VIOLATION)
            throw std::runtime_error("Cannot lock private Jade connection profiles.");
        Sleep(10);
    }
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Timed out waiting for private Jade connection profiles.");
    auto closer = [](void *value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closer)> lock(handle, closer);
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information) ||
        information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Private profile lock cannot be a reparse point.");
    auto values = read();
    if (values["profiles"].contains(name)) {
        if (values["profiles"][name] == settings)
            return;
        throw std::runtime_error(
            "Connection profile '" + name +
            "' already points to another connection. Choose a different --profile name.");
    }
    values["profiles"][name] = settings;
    auto temporary = parent / (L".connections-" + wide(uniqueId()) + L".tmp");
    PrivateFile::write(temporary, values.dump(2) + "\n");
    if (!MoveFileExW(temporary.c_str(), file_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        auto error = GetLastError();
        PrivateFile::remove(temporary);
        throw std::runtime_error("Cannot save Jade connection profiles (Windows " +
                                 std::to_string(error) + ").");
    }
}
} // namespace jade::native
