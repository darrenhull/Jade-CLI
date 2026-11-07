#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace jade::tests {
class NativeOidFixture {
    static std::string bytes(const std::filesystem::path &file) {
        if (!std::filesystem::is_regular_file(file) ||
            std::filesystem::file_size(file) > 32 * 1024 * 1024)
            throw std::runtime_error("Missing or oversized native fixture input.");
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot read native fixture input.");
        return {std::istreambuf_iterator<char>(stream), {}};
    }
    static std::string trim(std::string text) {
        const auto begin = text.find_first_not_of(" \t\r\n");
        return begin == std::string::npos
                   ? ""
                   : text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
    }
    static std::string lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }
    static bool inside(const std::filesystem::path &root, const std::filesystem::path &value) {
        const auto parent = std::filesystem::weakly_canonical(root),
                   child = std::filesystem::weakly_canonical(value);
        auto a = parent.begin(), b = child.begin();
        for (; a != parent.end(); ++a, ++b)
            if (b == child.end() ||
                CompareStringOrdinal(a->c_str(), -1, b->c_str(), -1, TRUE) != CSTR_EQUAL)
                return false;
        return b != child.end();
    }

  public:
    static std::filesystem::path seed(const std::filesystem::path &value) {
        const auto root = std::filesystem::canonical(value);
        if (root.parent_path() !=
                std::filesystem::canonical(std::filesystem::temp_directory_path()) ||
            !root.filename().wstring().starts_with(L"jade-cli-integration-seed-") ||
            !inside(root, root / L"system/_control.dat") || !inside(root, root / L"working.ini") ||
            !std::filesystem::is_regular_file(root / L"system/_control.dat"))
            throw std::runtime_error(
                "Use a disposable TEMP integration seed, never developer Source.");
        std::istringstream input(bytes(root / L"working.ini"));
        std::string line, section;
        std::set<std::string> seen;
        bool journal = false, transient = false;
        const std::regex heading(R"(^\[([^\]]+)\])"), setting(R"(^([^=]+?)\s*=\s*(.*)$)");
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            std::smatch match;
            if (std::regex_search(line, match, heading)) {
                section = lower(trim(match[1]));
                continue;
            }
            if (!std::regex_match(line, match, setting))
                continue;
            const auto key = lower(trim(match[1])), directory = trim(match[2]);
            if (!key.ends_with("directory"))
                continue;
            if (!seen.insert(section + "::" + key).second)
                throw std::runtime_error("Duplicate native fixture directory setting.");
            const bool required = (section == "persistentdb" && key == "journalrootdirectory") ||
                                  (section == "transientdb" && key == "filedirectory");
            if (lower(directory) == "<default>" && !required)
                continue;
            const auto path = std::filesystem::u8path(directory);
            if (directory.empty() || !path.is_absolute() || !inside(root, path))
                throw std::runtime_error(
                    "Native fixture mutable INI directory escapes its own root.");
            if (section == "persistentdb" && key == "journalrootdirectory")
                journal = true;
            if (section == "transientdb" && key == "filedirectory")
                transient = true;
        }
        if (!journal || !transient)
            throw std::runtime_error(
                "Native fixture requires explicit owned journal/transient directories.");
        return root;
    }
    template <class Hash>
    static std::filesystem::path distribution(const std::filesystem::path &value,
                                              const std::string &version, Hash hash) {
        const auto root = std::filesystem::canonical(value);
        const auto manifest = nlohmann::json::parse(bytes(root / L"distribution.json"));
        if (manifest.value("formatVersion", 0) != 2 ||
            manifest.value("runtime", "") != "native-cpp" ||
            manifest.value("platform", "") != "win32" || manifest.value("arch", "") != "x64" ||
            manifest.value("packageVersion", "") != version ||
            manifest.value("oidFormat", "") != "jade-native" ||
            manifest.value("executable", "") != "jade-cli.exe" ||
            !manifest.at("runtimeFiles").is_object())
            throw std::runtime_error(
                "Require a version-matched staged native Jade OID distribution.");
        auto files = manifest.at("runtimeFiles");
        files["jade-cli.exe"] = manifest.at("executableSha256");
        for (const auto *helper : {"jade-inspect-helper.exe", "jade-test-helper.exe",
                                   "jade-debug-helper.exe", "jade-native-helper.exe"})
            if (!files.contains(std::string("native/") + helper))
                throw std::runtime_error("Staged native helper missing.");
        for (const auto &[name, digest] : files.items()) {
            const auto relative = std::filesystem::u8path(name);
            if (relative.is_absolute() || !inside(root, root / relative) || !digest.is_string() ||
                !std::regex_match(digest.get<std::string>(), std::regex("[0-9a-fA-F]{64}")) ||
                lower(hash(bytes(root / relative))) != lower(digest.get<std::string>()))
                throw std::runtime_error("Staged native distribution path or checksum is invalid.");
        }
        return root;
    }
};
} // namespace jade::tests
