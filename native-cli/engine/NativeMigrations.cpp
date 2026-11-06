#include "NativeMigrations.hpp"
#include "NativeTimeout.hpp"
#include "PrivateFile.hpp"
#include <regex>
namespace jade::native {
std::vector<NativeRename> NativeMigrations::parse(const std::string &text,
                                                  const std::string &file) {
    std::smatch version;
    if (!std::regex_search(text, std::regex(R"(^\s*JadeCommandFile\b)", std::regex::icase)) ||
        !std::regex_search(
            text, version,
            std::regex(R"rx(JadeVersionNumber\s+"?([\d.]+)"?)rx", std::regex::icase)))
        throw std::runtime_error("Invalid native command file: " + file);
    std::vector<NativeRename> ops;
    std::istringstream lines(text);
    std::string line;
    std::regex skip(
        R"(^\s*(?:JadeCommandFile|JadeVersionNumber\b.*|Commands|//.*|AbortOnError\s+(?:True|False))\s*$)",
        std::regex::icase),
        rename(R"(^\s*Rename\s+Class\s+([A-Za-z_]\w*)::([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*$)",
               std::regex::icase);
    while (std::getline(lines, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos || std::regex_match(line, skip))
            continue;
        std::smatch m;
        if (!std::regex_match(line, m, rename))
            throw std::runtime_error(
                "Tracked native migrations accept Rename Class commands only: " + file + " (" +
                line + ").");
        ops.push_back({file, version[1], m[1], m[2], m[3]});
    }
    return ops;
}
std::vector<NativeRename> NativeMigrations::plan(const std::vector<NativeRename> &operations,
                                                 const std::set<std::string> &installed,
                                                 const std::set<std::string> &source) {
    auto current = installed;
    std::vector<NativeRename> pending;
    std::map<std::string, std::string> successors;
    for (auto &op : operations)
        successors[op.schema + "::" + op.oldName] = op.schema + "::" + op.newName;
    for (auto &op : operations) {
        auto old = op.schema + "::" + op.oldName, next = op.schema + "::" + op.newName,
             final = next;
        std::set<std::string> seen;
        while (successors.contains(final) && seen.insert(final).second)
            final = successors[final];
        if (!source.contains(final))
            throw std::runtime_error("Rename source declarations/references to " + final +
                                     " before applying " + op.file);
        if (current.contains(old) && current.contains(next))
            throw std::runtime_error("Rename source and target both exist: " + old + " / " + next);
        if (current.erase(old)) {
            pending.push_back(op);
            current.insert(next);
        }
    }
    return pending;
}
void NativeMigrations::apply(const ProjectModel &project, const json &working,
                             const std::filesystem::path &distributionRoot,
                             NativeProcess::Output output, std::atomic_bool *cancelled) {
    if (project.migrations.empty())
        return;
    std::vector<NativeRename> operations;
    json signature = json::array();
    for (auto &e : project.migrations) {
        auto ops = parse(e.text, utf8(e.file.wstring()));
        operations.insert(operations.end(), ops.begin(), ops.end());
        signature.push_back({utf8(e.file.wstring()), e.hash});
    }
    auto hash = sha256(signature.dump());
    auto root = std::filesystem::path(wide(working.at("root")));
    auto ledger = root / L"renames-applied-native.json";
    if (std::filesystem::exists(ledger) &&
        json::parse(readText(ledger)).value("signature", "") == hash)
        return;
    auto helper = distributionRoot / L"native" / L"jade-inspect-helper.exe";
    if (!std::filesystem::is_regular_file(helper))
        helper = distributionRoot / L"native-bin" / L"jade-inspect-helper.exe";
    auto config = working;
    config["readOnly"] = true;
    config["server"] = "singleUser";
    config["schema"] = "RootSchema";
    auto file = root / L"inspection-sessions" / (uniqueId() + ".json");
    PrivateFile::write(file, config.dump());
    std::set<std::string> current;
    try {
        {
            NativeProcess inspector;
            inspector.start(helper, {file.wstring()}, root);
            auto timeout = working.value("timeoutMilliseconds", 120000U);
            for (;;) {
                auto p = json::parse(inspector.line(cancelled, timeout, output), nullptr, false);
                if (p.is_discarded())
                    continue;
                if (p.value("event", "") == "error")
                    throw std::runtime_error(p.value("message", "Native class inspection failed."));
                if (p.value("event", "") == "ready") {
                    if (!p.value("readOnly", false))
                        throw std::runtime_error("Native class inspection must be read-only.");
                    break;
                }
            }
            inspector.write(
                json{{"id", 1}, {"command", "catalog"}, {"includeSystem", false}}.dump() + "\n");
            for (;;) {
                auto p = json::parse(inspector.line(cancelled, timeout, output), nullptr, false);
                if (p.is_discarded() || p.value("id", 0) != 1)
                    continue;
                if (!p.value("success", false))
                    throw std::runtime_error(p.value("message", "Native class catalog failed."));
                for (auto &s : p.at("body").at("schemas"))
                    for (auto &c : s.at("classes"))
                        current.insert(s.at("name").get<std::string>() +
                                       "::" + c.at("name").get<std::string>());
                break;
            }
            inspector.write(json{{"id", 2}, {"command", "close"}}.dump() + "\n");
            try {
                inspector.line(nullptr, 3000, output);
                inspector.waitExit(3000);
            } catch (...) {
            }
        }
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(file, error);
        throw;
    }
    PrivateFile::remove(file);
    std::set<std::string> source;
    for (auto &s : project.schemas)
        for (auto &e : s.classes)
            source.insert(s.name + "::" + e.model.classes[0].name);
    for (auto &e : project.loose)
        for (auto &c : e.model.classes)
            source.insert(e.model.schema + "::" + c.name);
    auto pending = plan(operations, current, source);
    for (auto &op : pending) {
        auto command = root / L"migrations" / (uniqueId() + ".jcf");
        writeText(command, "JadeCommandFile\r\nJadeVersionNumber " + op.version +
                               "\r\nCommands\r\nRename Class " + op.schema + "::" + op.oldName +
                               " " + op.newName + "\r\n");
        if (output)
            output("Native RENAME " + op.schema + "::" + op.oldName + " -> " + op.newName + "\n");
        NativeProcess loader;
        auto result = loader.run(
            std::filesystem::path(wide(working.at("installPath"))) / L"bin" / L"jadloadb.exe",
            {L"path=" + wide(working.at("databasePath")), L"ini=" + wide(working.at("iniPath")),
             L"server=singleUser", L"commandFile=" + command.wstring(),
             L"loadStyle=currentSchemaVersion", L"replayableReorg=true", L"ShowProgress=true"},
            root, cancelled, working.value("timeoutMilliseconds", 120000U), output);
        if (result.timedOut)
            throw NativeTimeout("Native class rename timed out.");
        if (!result.success())
            throw std::runtime_error("Native class rename failed (exit " +
                                     std::to_string(result.code) + "). " + result.output);
    }
    writeText(ledger, json{{"signature", hash}}.dump(2));
}
} // namespace jade::native
