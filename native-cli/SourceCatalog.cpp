#include "SourceCatalog.hpp"
#include "engine/ProjectModel.hpp"
#include <algorithm>
#include <set>
namespace jade::native {
namespace fs = std::filesystem;
namespace {
std::string lower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return v;
}
} // namespace
std::vector<fs::path> SourceCatalog::files(const std::vector<fs::path> &roots) const {
    std::set<fs::path> found;
    for (const auto &root : roots)
        for (const auto &file : ProjectModel::listFiles(root))
            if (lower(file.extension().string()) == ".cls")
                found.insert(file);
    return {found.begin(), found.end()};
}
std::vector<fs::path> SourceCatalog::buildInputs(const std::vector<fs::path> &roots) const {
    std::set<fs::path> classes, headers;
    for (const auto &root : roots)
        for (const auto &file : ProjectModel::listFiles(root)) {
            const auto extension = lower(file.extension().string());
            if (extension == ".cls") classes.insert(file);
            else if (extension == ".scm" || lower(file.filename().string()) == "schemadefinitions.sec")
                headers.insert(file);
        }
    const auto &selected = classes.empty() ? headers : classes;
    return {selected.begin(), selected.end()};
}
nlohmann::json SourceCatalog::query(const std::string &command, const nlohmann::json &config,
                                    const std::string &query, size_t offset, size_t limit) const {
    std::vector<fs::path> roots;
    for (const auto &r : config.value("workspaceRoots",
                                      nlohmann::json::array({utf8(fs::current_path().wstring())})))
        roots.push_back(fs::u8path(r.get<std::string>()));
    nlohmann::json rows = nlohmann::json::array();
    const auto needle = lower(query);
    for (const auto &file : files(roots)) {
        const auto model = SourceModel::parse(readText(file), utf8(file.wstring()));
        auto add = [&](const nlohmann::json &row) {
            if (needle.empty() || lower(row.dump()).find(needle) != std::string::npos)
                rows.push_back(row);
        };
        if (command == "source.search")
            for (const auto &cls : model.classes)
                add({{"schema", model.schema},
                     {"name", cls.name},
                     {"source", utf8(file.wstring())}});
        else if (model.classes.size() == 1)
            for (const auto &method : model.methods)
                if (method.unitTest && !method.ignored)
                    add({{"schema", model.schema},
                         {"owner", model.classes[0].name},
                         {"method", method.name},
                         {"source", utf8(file.wstring())},
                         {"key",
                          model.schema + "::" + model.classes[0].name + "::" + method.name}});
    }
    limit = std::clamp<size_t>(limit, 1, 100);
    const size_t total = rows.size();
    nlohmann::json page = nlohmann::json::array();
    for (size_t i = std::min(offset, total); i < total && i - offset < limit; ++i)
        page.push_back(rows[i]);
    return {{"code", 0}, {"passed", true}, {"total", total}, {"offset", offset}, {"rows", page}};
}
} // namespace jade::native
