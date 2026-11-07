#include "WorkspaceRunner.hpp"
#include "PrivateFile.hpp"
#include <chrono>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
namespace jade::native {
namespace {
std::string pathText(const std::filesystem::path &path) {
    return utf8(path.wstring());
}
std::string literal(const std::string &value) {
    std::string result = "'";
    for (const auto c : value) {
        if (c == '\'')
            result += "' & \"'\" & '";
        else
            result += c;
    }
    return result + "'";
}
std::string utf16(const std::string &value) {
    std::string result = "\xff\xfe";
    for (const auto c : wide(value)) {
        result += char(c & 255);
        result += char((c >> 8) & 255);
    }
    return result;
}
std::string consoleText(const std::string &value) {
    if (value.empty())
        return {};
    const auto count = MultiByteToWideChar(CP_ACP, 0, value.data(), int(value.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("Cannot decode native Workspace output.");
    std::wstring text(count, 0);
    if (!MultiByteToWideChar(CP_ACP, 0, value.data(), int(value.size()), text.data(), count))
        throw std::runtime_error("Cannot decode native Workspace output.");
    return utf8(text);
}
std::string head(const std::string &value, size_t limit) {
    auto end = std::min(value.size(), limit);
    while (end && end < value.size() && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
        --end;
    return value.substr(0, end);
}
std::string tail(const std::string &value, size_t limit) {
    auto start = value.size() > limit ? value.size() - limit : 0;
    while (start < value.size() && (static_cast<unsigned char>(value[start]) & 0xc0) == 0x80)
        ++start;
    return value.substr(start);
}
std::string normalizedLines(const std::string &value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            auto next = i;
            while (next < value.size() && value[next] == '\r')
                ++next;
            if (next < value.size() && value[next] == '\n') {
                i = next;
                result += '\n';
                continue;
            }
            result.append(value, i, next - i);
            i = next - 1;
            continue;
        }
        result += value[i];
    }
    return result;
}
std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string exceptionDelta(const std::filesystem::path &file, uintmax_t offset) {
    if (!std::filesystem::is_regular_file(file))
        return {};
    const auto size = std::filesystem::file_size(file);
    if (offset > size)
        offset = 0;
    if (size - offset > 32768)
        offset = size - 32768;
    std::ifstream stream(file, std::ios::binary);
    unsigned char bom[3]{};
    stream.read(reinterpret_cast<char *>(bom), 3);
    const bool unicode = bom[0] == 255 && bom[1] == 254;
    const bool utf8Bom = bom[0] == 239 && bom[1] == 187 && bom[2] == 191;
    if (unicode && offset % 2)
        ++offset;
    if (offset == 0)
        offset = unicode ? 2 : utf8Bom ? 3 : 0;
    stream.clear();
    stream.seekg(offset);
    auto bytes = std::string((std::istreambuf_iterator<char>(stream)), {});
    if (unicode) {
        std::wstring text;
        for (size_t i = 0; i + 1 < bytes.size(); i += 2)
            text += wchar_t(static_cast<unsigned char>(bytes[i]) |
                            (unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8));
        if (!text.empty() && text[0] >= 0xdc00 && text[0] <= 0xdfff)
            text.erase(0, 1);
        return utf8(text);
    }
    if (utf8Bom) {
        while (!bytes.empty() && (static_cast<unsigned char>(bytes[0]) & 0xc0) == 0x80)
            bytes.erase(0, 1);
        wide(bytes);
        return bytes;
    }
    return consoleText(bytes);
}
} // namespace
void WorkspaceRunner::validate(const json &request) {
    if (!request.contains("sourceText") || !request["sourceText"].is_string())
        throw std::runtime_error("Workspace sourceText must contain the exact code snapshot.");
    const auto source = request["sourceText"].get<std::string>();
    if (source.empty() || source.size() > 65536 || source.find('\0') != std::string::npos)
        throw std::runtime_error(
            "Workspace source must contain 1 to 65536 UTF-8 bytes without NUL.");
    wide(source);
    if (request.at("config").contains("workspaceRoots") &&
        (!request.at("config")["workspaceRoots"].is_array() ||
         request.at("config")["workspaceRoots"].empty()))
        throw std::runtime_error(
            "Configure at least one workspace root before native Workspace preparation.");
    const auto schema = request.at("config").value("schema", "");
    if (!std::regex_match(schema, std::regex("[A-Za-z_][A-Za-z0-9_]*")) || schema == "RootSchema")
        throw std::runtime_error("Choose a user schema for Workspace execution; RootSchema scripts "
                                 "are not supported by Jade.");
    for (const auto *key : {"lineOffset", "columnOffset"})
        if (request.contains(key) &&
            (!request[key].is_number_integer() || request[key].get<int64_t>() < 0 ||
             request[key].get<int64_t>() > 100000000))
            throw std::runtime_error(
                "Workspace source offsets must be nonnegative bounded integers.");
    if (request.contains("source") &&
        (!request["source"].is_string() || request["source"].get<std::string>().size() > 32768 ||
         request["source"].get<std::string>().find('\0') != std::string::npos))
        throw std::runtime_error("Workspace source must be a bounded URI label.");
}
std::string WorkspaceRunner::launcher(const std::string &source, const std::string &marker) {
    return "jadeWorkspaceRun();\r\nvars\r\n err, pos, len : Integer;\r\n result : "
           "Any;\r\nbegin\r\n write " +
           literal(marker + ":BEGIN") +
           ";\r\n result := process.executeScript('workspace', self.class, currentSchema, " +
           literal(source) + ", true, null, err, pos, len, self);\r\n write " +
           literal(marker + ":RESULT:") +
           " & err.String & ':' & pos.String & ':' & len.String;\r\n if err <> 0 then\r\n write " +
           literal(marker + ":MESSAGE:") + " & process.getErrorText(err);\r\n endif;\r\nend;\r\n";
}
json WorkspaceRunner::result(const ProcessResult &process, const std::string &marker,
                             const json &request, const std::string &database, double elapsed,
                             const std::string &exceptionLog) {
    const auto source = request.value("source", "jade-workspace:untitled");
    json report = {{"compiled", false},
                   {"executed", false},
                   {"success", false},
                   {"output", ""},
                   {"diagnostics", json::array()},
                   {"source", source},
                   {"workingDatabase", database},
                   {"elapsedMilliseconds", elapsed},
                   {"independentLocals", true}};
    const auto normalized = normalizedLines(process.output);
    const auto begin = normalized.find(marker + ":BEGIN\n");
    const auto start = begin == std::string::npos ? std::string::npos : begin + marker.size() + 7;
    const auto end = start == std::string::npos ? std::string::npos
                                                : normalized.find(marker + ":RESULT:", start);
    int code = process.code == 0 ? 2 : process.code;
    const auto nativeException = start == std::string::npos
                                     ? std::string::npos
                                     : normalized.find("Script terminated with exception ", start);
    if (start != std::string::npos) {
        const auto outputEnd = end != std::string::npos ? end : nativeException;
        const auto contents = normalized.substr(
            start, outputEnd == std::string::npos ? std::string::npos : outputEnd - start);
        report["output"] = head(contents, 256 * 1024);
        report["outputTruncated"] = contents.size() > 256 * 1024;
    }
    if (end != std::string::npos) {
        const auto lineEnd = normalized.find('\n', end);
        auto status = normalized.substr(end + marker.size() + 8, lineEnd - end - marker.size() - 8);
        std::smatch match;
        if (std::regex_match(status, match, std::regex("([0-9]{1,9}):([0-9]{1,9}):([0-9]{1,9})"))) {
            const int nativeCode = std::stoi(match[1]);
            code = nativeCode;
            report["compiled"] = nativeCode == 0;
            report["executed"] = nativeCode == 0;
            if (nativeCode) {
                const auto text = wide(request.at("sourceText").get<std::string>());
                const auto position = size_t(std::stoul(match[2]));
                const auto length = size_t(std::stoul(match[3]));
                size_t line = 1, column = 1;
                for (size_t i = 0; i < std::min(position, text.size()); ++i) {
                    if (text[i] == L'\n') {
                        ++line;
                        column = 1;
                    } else if (text[i] != L'\r')
                        ++column;
                }
                column += line == 1 ? request.value("columnOffset", 0) : 0;
                line += request.value("lineOffset", 0);
                auto message = "Jade compiler error " + std::to_string(nativeCode);
                const auto messageStart = normalized.find(marker + ":MESSAGE:", end);
                if (messageStart != std::string::npos) {
                    const auto begin = messageStart + marker.size() + 9;
                    message =
                        head(normalized.substr(begin, normalized.find('\n', begin) - begin), 2048);
                }
                report["diagnostics"].push_back({{"source", source},
                                                 {"compiler_error", nativeCode},
                                                 {"message", message},
                                                 {"position", position},
                                                 {"length", length},
                                                 {"line", line},
                                                 {"column", column}});
            }
        }
    }
    std::smatch runtime;
    const auto log = normalizedLines(exceptionLog);
    if (end == std::string::npos && nativeException != std::string::npos &&
        std::regex_search(log, runtime,
                          std::regex(R"(\b\w*Exception:\s*(\d+)[^\n]*\n\*\s*([^\n]+))"))) {
        code = std::stoi(runtime[1]);
        const auto message = head(runtime[2], 2048);
        report["runtime_error"] = code;
        report["compiled"] = true;
        report["executed"] = true;
        report["error"] = message;
        std::smatch frame;
        if (std::regex_search(
                log, frame,
                std::regex(R"(Method:\s*JadeScript::workspace\([^\n]*\)\s*--\s*([^\n]+))"))) {
            const auto needle = trim(frame[1]);
            std::istringstream lines(request.at("sourceText").get<std::string>());
            std::string line;
            size_t number = 0, position = 0;
            json found;
            while (std::getline(lines, line)) {
                ++number;
                if (trim(line) == needle) {
                    if (!found.is_null()) {
                        found = nullptr;
                        break;
                    }
                    const auto leading = line.find_first_not_of(" \t");
                    const auto column = wide(line.substr(0, leading)).size() + 1;
                    found = {
                        {"source", source},
                        {"compiler_error", code},
                        {"runtime_error", code},
                        {"message", message},
                        {"line", number + request.value("lineOffset", 0)},
                        {"column", column + (number == 1 ? request.value("columnOffset", 0) : 0)},
                        {"position", position + column - 1},
                        {"length", wide(needle).size()}};
                }
                position += wide(line).size() + 1;
            }
            if (!found.is_null())
                report["diagnostics"].push_back(found);
        }
    }
    const bool passed = code == 0 && process.success() && end != std::string::npos;
    if (!passed) {
        if (process.cancelled)
            report["error"] = "Workspace execution cancelled.";
        else if (process.timedOut)
            report["error"] = "Workspace execution timed out.";
        else if (!report["diagnostics"].empty())
            report["error"] = report["diagnostics"][0]["message"];
        else if (!report.contains("error"))
            report["error"] =
                "Native Workspace execution did not complete. " + tail(normalized, 32768);
        if (code == 0)
            code = process.code == 0 ? 2 : process.code;
    }
    report["success"] = passed;
    return {{"code", code},
            {"passed", passed},
            {"cancelled", process.cancelled},
            {"timedOut", process.timedOut},
            {"report", report}};
}
json WorkspaceRunner::execute(const json &request, NativeProcess::Output output,
                              std::atomic_bool *cancelled) {
    validate(request);
    const auto config = request.at("config");
    if (cancelled && cancelled->load())
        return {{"code", 130},
                {"passed", false},
                {"cancelled", true},
                {"report", {{"error", "Workspace cancelled before execution."}}}};
    std::vector<std::filesystem::path> roots, files;
    for (const auto &value : config.at("workspaceRoots"))
        roots.emplace_back(wide(value));
    for (const auto &value : config.value("files", json::array()))
        files.emplace_back(wide(value));
    auto project = ProjectModel::scan(roots, files).schemaScope(config.at("schema"));
    auto build = config;
    build["projectBuild"] = false;
    build["program"] = !project.schemas.empty() ? pathText(project.schemas[0].header.file)
                       : !project.loose.empty() ? pathText(project.loose[0].file)
                                                : pathText(roots[0] / ".jade-workspace");
    const auto prepared = working_.executeProject({{"command", "build"}, {"config", build}},
                                                  std::move(project), true, output, cancelled);
    if (!prepared.value("passed", false))
        return prepared;
    auto working = working_.prepareDatabase(config, output, cancelled);
    working_.disconnect();
    const auto directory =
        std::filesystem::path(wide(working.at("root"))) / "workspaces" / uniqueId();
    PrivateFile::protectDirectory(directory);
    const auto marker = "JADE_WORKSPACE_" + uniqueId();
    const auto script = directory / "run.jade", ini = directory / "run.ini";
    const auto start = std::chrono::steady_clock::now();
    NativeProcess process;
    const auto exceptionFile =
        std::filesystem::path(wide(working.at("root"))) / "logs" / "rootschemaapp.log";
    const auto exceptionOffset = std::filesystem::is_regular_file(exceptionFile)
                                     ? std::filesystem::file_size(exceptionFile)
                                     : 0;
    try {
        PrivateFile::write(script, utf16(launcher(request.at("sourceText"), marker)));
        PrivateFile::write(ini,
                           utf16(readText(std::filesystem::path(wide(working.at("iniPath"))))));
        if (!WritePrivateProfileStringW(L"NonGuiClient", L"ReadWriteStdio", L"true", ini.c_str()))
            throw std::runtime_error("Cannot configure native Workspace standard output.");
        auto native = process.run(
            std::filesystem::path(wide(config.at("installPath"))) / "bin" / "jadclient.exe",
            {L"path=" + wide(working.at("databasePath")), L"ini=" + ini.wstring(),
             L"server=singleUser", L"schema=" + wide(config.at("schema")), L"app=RootSchemaApp",
             L"executeScript=" + script.wstring()},
            directory, cancelled, config.value("timeoutMilliseconds", 120000u), {}, true);
        // jadclient emits Windows ANSI stdio in the verified runtime. Decode
        // complete output before it enters a UTF-8 JSON protocol; do not emit
        // raw chunks, which may contain half a multibyte code-page character.
        native.output = consoleText(native.output);
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        auto response = result(native, marker, request, working.at("databasePath"), elapsed,
                               exceptionDelta(exceptionFile, exceptionOffset));
        response["report"]["outputEncoding"] = "windows-" + std::to_string(GetACP());
        response["report"]["outputCharacterLossPossible"] = GetACP() != CP_UTF8;
        response["report"]["interactiveInputSupported"] = false;
        PrivateFile::remove(script);
        PrivateFile::remove(ini);
        return response;
    } catch (...) {
        process.stop();
        auto original = std::current_exception();
        try {
            PrivateFile::remove(script);
            PrivateFile::remove(ini);
        } catch (const std::exception &cleanup) {
            if (output)
                output(std::string("Workspace cleanup failed: ") + cleanup.what() + "\n");
        }
        std::rethrow_exception(original);
    }
}
} // namespace jade::native
