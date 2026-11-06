#include "NativeReports.hpp"
#include <regex>
#include <map>
#include <set>
namespace jade::native {
json NativeReports::tests(const std::string &log, const SourceModel &model,
                          const std::vector<std::string> &selection) {
    std::vector<std::string> names = selection;
    if (names.empty())
        for (auto &m : model.methods)
            if (m.unitTest)
                names.push_back(m.name);
    std::set<std::string> ignored;
    for (auto &m : model.methods)
        if (m.ignored && std::find(names.begin(), names.end(), m.name) != names.end())
            ignored.insert(m.name);
    std::smatch summary;
    bool hasSummary = false;
    std::regex count(R"(total tests=(\d+) failed=(\d+) skipped=(\d+) succeeded=(\d+))");
    for (std::sregex_iterator it(log.begin(), log.end(), count), end; it != end; ++it) {
        summary = *it;
        hasSummary = true;
    }
    std::string owner = model.classes.empty() ? "" : model.classes[0].name;
    std::map<std::string, json> failures;
    std::vector<std::string> unidentified;
    std::regex failure(R"( : (\w+)::(\w+)::(\w+): ([^\r\n]+))");
    std::vector<std::smatch> entries;
    for (std::sregex_iterator it(log.begin(), log.end(), failure), end; it != end; ++it)
        entries.push_back(*it);
    std::regex frame(R"((\w+)::(\w+)::(\w+)\((\d+)\))");
    for (size_t i = 0; i < entries.size(); ++i) {
        auto &e = entries[i];
        auto block = log.substr(e.position(),
                                (i + 1 < entries.size() ? entries[i + 1].position() : log.size()) -
                                    e.position());
        std::string method;
        bool direct = e[1] == model.schema && e[2] == owner &&
                      std::find(names.begin(), names.end(), e[3]) != names.end();
        if (direct)
            method = e[3];
        std::set<std::string> callers;
        for (std::sregex_iterator it(block.begin(), block.end(), frame), end; it != end; ++it)
            if ((*it)[1] == model.schema && (*it)[2] == owner &&
                std::find(names.begin(), names.end(), (*it)[3]) != names.end())
                callers.insert((*it)[3]);
        if (method.empty() && callers.size() == 1)
            method = *callers.begin();
        if (method.empty() && names.size() == 1 && hasSummary && summary[1] == "1" &&
            summary[2] == "1")
            method = names[0];
        auto qualified = e[1].str() + "::" + e[2].str() + "::" + e[3].str() + ": " + e[4].str();
        if (method.empty()) {
            unidentified.push_back(qualified);
            continue;
        }
        size_t line = 1;
        for (auto &m : model.methods)
            if (m.name == method)
                line = m.line;
        json row = {{"owner", owner},
                    {"method", method},
                    {"status", "failed"},
                    {"message", direct ? e[4].str() : qualified},
                    {"source", model.source},
                    {"line", line},
                    {"column", 1}};
        if (failures.contains(method))
            row["message"] = failures[method]["message"].get<std::string>() + "\n" +
                             row["message"].get<std::string>();
        failures[method] = row;
    }
    int failed = hasSummary ? std::stoi(summary[2]) : 0;
    bool complete = hasSummary && unidentified.empty() && std::stoi(summary[1]) == names.size() &&
                    failed == failures.size() && std::stoi(summary[3]) == ignored.size() &&
                    std::stoi(summary[4]) + failed + ignored.size() == names.size();
    json result = {{"tests_completed", complete},
                   {"cleanup_complete", complete},
                   {"tests_selected", names.size()},
                   {"failures", hasSummary ? json(failed) : json(nullptr)},
                   {"results", json::array()}};
    if (complete)
        for (auto &name : names)
            result["results"].push_back(
                failures.contains(name)
                    ? failures[name]
                    : json{{"owner", owner},
                           {"method", name},
                           {"status", ignored.contains(name) ? "skipped" : "passed"}});
    else {
        std::string message = "Native test log did not identify a complete result for every "
                              "selected method. See Jade Tests output.";
        for (auto &e : unidentified)
            message += " Native failure: " + e;
        result["error"] = message;
    }
    return result;
}
json NativeReports::diagnostics(const std::string &output,
                                const std::vector<SourceEntry> &sources) {
    json r = json::array();
    std::regex errors(
        R"(Compile error (\d+) \(([^\r\n]+)\) in file ([^\r\n]+) in (\w+)::(\w+)::(\w+) at line (\d+):)",
        std::regex::icase);
    for (std::sregex_iterator it(output.begin(), output.end(), errors), end; it != end; ++it) {
        auto &m = *it;
        for (auto &e : sources)
            if (e.model.schema == m[4] && !e.model.classes.empty() &&
                e.model.classes[0].name == m[5]) {
                size_t line = 1;
                for (auto &method : e.model.methods)
                    if (method.name == m[6])
                        line = method.bodyLine + std::max(0, std::stoi(m[7]) - 1);
                r.push_back({{"source", utf8(e.file.wstring())},
                             {"schema", m[4].str()},
                             {"owner", m[5].str()},
                             {"method", m[6].str()},
                             {"compiler_error", std::stoi(m[1])},
                             {"message", m[2].str()},
                             {"line", line},
                             {"column", 1}});
                break;
            }
    }
    std::regex structural(
        R"(Compile Error (\d+)\s*-\s*([^\r\n]+)(?:\s*\r?\nAt line (\d+):\s*\r?\n([^\r\n]+))?)",
        std::regex::icase);
    for (std::sregex_iterator it(output.begin(), output.end(), structural), end; it != end; ++it) {
        auto m = *it;
        std::string source;
        size_t line = std::max(1, m[3].matched ? std::stoi(m[3]) : 1);
        std::string needle = m[4];
        auto trim = [](std::string s) {
            auto first = s.find_first_not_of(" \t\r\n"), last = s.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
        };
        needle = trim(needle);
        if (!needle.empty())
            for (auto &e : sources) {
                std::istringstream lines(e.text);
                std::string s;
                size_t number = 0;
                while (std::getline(lines, s)) {
                    ++number;
                    if (trim(s) == needle) {
                        if (!source.empty()) {
                            source.clear();
                            goto ambiguous;
                        }
                        source = utf8(e.file.wstring());
                        line = number;
                    }
                }
            }
    ambiguous:
        if (!source.empty())
            r.push_back({{"source", source},
                         {"compiler_error", std::stoi(m[1])},
                         {"message", m[2].str()},
                         {"line", line},
                         {"column", 1}});
    }
    if (!r.empty())
        return r;

    // Persistent native loads write the authoritative structural position to
    // compiler.log instead of printing the older "At line" snippet. Only
    // accept files supplied by this load; never open a path from log text.
    std::regex location(
        R"(Schema compile failed: file=([^\r\n]+?), schema=(\w+),[^\r\n]*?error=(\d+), pos=(\d+), len=(\d+), lineno=(\d+))",
        std::regex::icase);
    for (std::sregex_iterator it(output.begin(), output.end(), location), end; it != end; ++it) {
        auto m = *it;
        const SourceEntry *loaded = nullptr;
        for (auto &entry : sources)
            if (pathKey(entry.file) == pathKey(wide(m[1].str()))) {
                loaded = &entry;
                break;
            }
        if (!loaded)
            continue;
        const auto nativeLine = std::stoull(m[6]), position = std::stoull(m[4]);
        const auto loadedText = wide(loaded->text);
        size_t start = 0, lineNumber = 1;
        while (lineNumber < nativeLine && start < loadedText.size()) {
            auto newline = loadedText.find(L'\n', start);
            if (newline == std::wstring::npos) {
                start = loadedText.size();
                break;
            }
            start = newline + 1;
            ++lineNumber;
        }
        if (!nativeLine || lineNumber != nativeLine || start >= loadedText.size())
            continue;
        auto finish = loadedText.find(L'\n', start);
        if (finish == std::wstring::npos)
            finish = loadedText.size();
        auto originalLine = loadedText.substr(start, finish - start);
        const auto first = originalLine.find_first_not_of(L" \t\r"),
                   last = originalLine.find_last_not_of(L" \t\r");
        auto needle = first == std::wstring::npos ? std::wstring()
                                                  : originalLine.substr(first, last - first + 1);
        const auto nativeColumn = position >= start && position < finish ? position - start + 1 : 1;
        const SourceEntry *mapped = nullptr;
        size_t mappedLine = 0, mappedLeading = 0;
        bool ambiguous = false;
        for (auto &entry : sources) {
            if (&entry == loaded || entry.model.schema != m[2])
                continue;
            auto text = wide(entry.text);
            for (size_t at = 0, number = 1; at < text.size(); ++number) {
                auto next = text.find(L'\n', at);
                if (next == std::wstring::npos)
                    next = text.size();
                auto line = text.substr(at, next - at);
                auto leading = line.find_first_not_of(L" \t\r"),
                     trailing = line.find_last_not_of(L" \t\r");
                if (!needle.empty() && leading != std::wstring::npos &&
                    line.substr(leading, trailing - leading + 1) == needle) {
                    if (mapped)
                        ambiguous = true;
                    mapped = &entry;
                    mappedLine = number;
                    mappedLeading = leading;
                }
                at = next + 1;
            }
        }
        if (ambiguous)
            mapped = nullptr;
        std::string message = "Native Jade compiler error " + m[3].str();
        for (std::sregex_iterator error(output.begin(), output.end(), structural), stop;
             error != stop; ++error)
            if ((*error)[1] == m[3]) {
                message = (*error)[2];
                break;
            }
        const auto column = mapped && first != std::wstring::npos && nativeColumn > first
                                ? nativeColumn - first + mappedLeading
                                : nativeColumn;
        r.push_back({{"source", utf8((mapped ? mapped->file : loaded->file).wstring())},
                     {"schema", m[2].str()},
                     {"compiler_error", std::stoi(m[3])},
                     {"message", message},
                     {"line", mapped ? mappedLine : nativeLine},
                     {"column", column},
                     {"length", std::stoull(m[5])},
                     {"generated_source", mapped == nullptr}});
    }
    return r;
}
} // namespace jade::native
