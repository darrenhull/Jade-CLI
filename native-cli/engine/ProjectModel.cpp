#include "ProjectModel.hpp"
#include "DdxMerger.hpp"
#include <fstream>
#include <regex>
#include <algorithm>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
namespace jade::native {
static std::string readBytes(const std::filesystem::path &file) {
    std::ifstream f(file, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot read " + utf8(file.wstring()));
    std::string s((std::istreambuf_iterator<char>(f)), {});
    return s;
}
static std::string decodeText(std::string s) {
    if (s.starts_with("\xFF\xFE")) {
        std::wstring w;
        for (size_t i = 2; i + 1 < s.size(); i += 2)
            w += wchar_t(static_cast<unsigned char>(s[i]) |
                         (unsigned(static_cast<unsigned char>(s[i + 1])) << 8));
        return utf8(w);
    }
    if (s.starts_with("\xEF\xBB\xBF"))
        s.erase(0, 3);
    return s;
}
std::string readText(const std::filesystem::path &file) {
    return decodeText(readBytes(file));
}
void writeText(const std::filesystem::path &file, const std::string &text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(text.data(), text.size()))
        throw std::runtime_error("Cannot write " + utf8(file.wstring()));
}
std::string sha256(const std::string &data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    DWORD size = 0, count = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA256.");
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size),
                      &count, 0);
    std::vector<unsigned char> object(size);
    auto cleanup = [&] {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    };
    if (BCryptCreateHash(alg, &hash, object.data(), size, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())),
                       ULONG(data.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest, 32, 0) < 0) {
        cleanup();
        throw std::runtime_error("Cannot hash source.");
    }
    cleanup();
    std::ostringstream out;
    for (auto c : digest)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    return out.str();
}
std::string uniqueId() {
    unsigned char data[16];
    if (BCryptGenRandom(nullptr, data, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate private identity.");
    std::ostringstream out;
    for (auto c : data)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    return out.str();
}
std::string pathKey(const std::filesystem::path &file) {
    auto value = std::filesystem::weakly_canonical(std::filesystem::absolute(file)).wstring();
    // Use the Windows ordinal uppercase table rather than linguistic casing.
    // Filesystem path identities are not language-aware text comparisons.
    auto count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(),
                               int(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!count)
        throw std::runtime_error("Cannot normalize a Windows source path.");
    std::wstring folded(count, 0);
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), int(value.size()),
                       folded.data(), count, nullptr, nullptr, 0))
        throw std::runtime_error("Cannot normalize a Windows source path.");
    return utf8(folded);
}
void stageSourceFile(const SourceEntry &source, const std::filesystem::path &target) {
    auto bytes = readBytes(source.file);
    if (source.hash.empty() || sha256(bytes) != source.hash)
        throw std::runtime_error("Source changed while preparing native load. Compile again: " +
                                 utf8(source.file.wstring()));
    writeText(target, bytes);
}
bool contains(const std::filesystem::path &root, const std::filesystem::path &child) {
    auto a = pathKey(root), b = pathKey(child);
    return a == b ||
           (b.size() > a.size() && b.starts_with(a) && (b[a.size()] == '\\' || b[a.size()] == '/'));
}
std::vector<std::filesystem::path> ProjectModel::listFiles(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        auto p = it->path();
        if (it->is_directory()) {
            auto name = p.filename().wstring();
            if (name == L".git" || name == L"node_modules" || name == L"dist" ||
                std::filesystem::exists(p / L".jade-extraction") || it->is_symlink())
                it.disable_recursion_pending();
        } else if (it->is_regular_file()) {
            auto ext = p.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".cls" || ext == L".scm" || ext == L".sec" || ext == L".ddx" ||
                ext == L".ddb" || ext == L".jcf")
                files.push_back(std::filesystem::absolute(p));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
static size_t sectionStart(const std::string &code, const std::string &name) {
    for (size_t start = 0; start < code.size();) {
        auto end = code.find('\n', start);
        if (end == std::string::npos)
            end = code.size();
        auto first = code.find_first_not_of(" \t\r", start);
        auto last = end;
        while (last > start &&
               (code[last - 1] == ' ' || code[last - 1] == '\t' || code[last - 1] == '\r'))
            --last;
        if (first != std::string::npos && first < end &&
            code.compare(first, last - first, name) == 0)
            return start;
        start = end + 1;
    }
    return std::string::npos;
}
static bool section(const std::string &code, const std::string &name) {
    return sectionStart(code, name) != std::string::npos;
}
static SourceEntry entry(const std::filesystem::path &file) {
    SourceEntry e;
    e.file = file;
    e.extension = utf8(file.extension().wstring());
    std::transform(e.extension.begin(), e.extension.end(), e.extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    auto bytes = readBytes(file);
    e.text = decodeText(bytes);
    e.hash = sha256(bytes);
    e.model = SourceModel::parse(e.text, utf8(file.wstring()));
    auto marker = sectionStart(e.model.code, "typeSources");
    json signatures = json::array();
    for (auto &method : e.model.methods)
        signatures.push_back({method.name, method.flags});
    e.shape = sha256((marker == std::string::npos ? e.text : e.text.substr(0, marker)) +
                     signatures.dump());
    return e;
}
ProjectModel ProjectModel::scan(const std::vector<std::filesystem::path> &roots,
                                const std::vector<std::filesystem::path> &explicitFiles) {
    ProjectModel p;
    std::vector<SourceEntry> entries;
    std::set<std::string> known;
    for (auto &root : roots)
        for (auto &file : listFiles(root))
            if (known.insert(pathKey(file)).second)
                entries.push_back(entry(file));
    for (auto &file : explicitFiles) {
        if (!std::filesystem::is_regular_file(file))
            throw std::runtime_error("Source dependency is not a file: " + utf8(file.wstring()));
        if (known.insert(pathKey(file)).second)
            entries.push_back(entry(std::filesystem::absolute(file)));
    }
    std::map<std::string, size_t> names;
    for (auto &e : entries)
        if (e.extension == ".scm" || e.file.filename() == L"schemaDefinitions.sec") {
            auto &m = e.model;
            if (m.schema.empty())
                throw std::runtime_error("Schema header has no declaration: " +
                                         utf8(e.file.wstring()));
            if (m.schema == "RootSchema" || m.schema == "JadeSchema")
                continue;
            if (names.contains(m.schema))
                throw std::runtime_error("Duplicate schema declaration: " + m.schema);
            ProjectSchema s;
            s.name = m.schema;
            s.parent = m.parentSchema;
            s.folder =
                e.extension == ".scm" ? e.file.parent_path() / wide(s.name) : e.file.parent_path();
            s.header = e;
            names[s.name] = p.schemas.size();
            p.schemas.push_back(s);
        }
    for (auto &e : entries) {
        if (e.extension == ".jcf") {
            p.migrations.push_back(e);
            continue;
        }
        if (e.extension == ".scm" || e.file.filename() == L"schemaDefinitions.sec")
            continue;
        std::string schema = e.model.schema;
        if (schema.empty()) {
            size_t longest = 0;
            for (auto &s : p.schemas)
                if (contains(s.folder, e.file) && s.folder.native().size() > longest) {
                    schema = s.name;
                    longest = s.folder.native().size();
                }
        }
        if (!names.contains(schema)) {
            if (e.extension == ".cls")
                p.loose.push_back(e);
            continue;
        }
        auto &s = p.schemas[names.at(schema)];
        if (e.extension == ".cls") {
            if (e.model.classes.size() != 1) {
                s.classes.push_back(e);
                continue;
            }
            auto name = e.model.classes[0].name;
            for (auto &c : s.classes)
                if (!c.model.classes.empty() && c.model.classes[0].name == name)
                    throw std::runtime_error("Multiple complete class declarations: " + schema +
                                             "::" + name);
            s.classes.push_back(e);
        } else
            s.resources.push_back(e);
    }
    std::vector<ProjectSchema> ordered;
    std::set<std::string> active, done;
    std::function<void(size_t, bool)> add = [&](size_t index, bool packageEdge) {
        auto &s = p.schemas[index];
        if (done.contains(s.name))
            return;
        if (!active.insert(s.name).second) {
            if (packageEdge)
                return;
            throw std::runtime_error("Cyclic schema hierarchy: " + s.name);
        }
        if (names.contains(s.parent))
            add(names.at(s.parent), false);
        std::set<std::string> imports;
        for (auto &resource : s.resources)
            if (resource.file.filename() == L"importedPackageDefinitions.sec")
                imports.insert(resource.model.identifiers.begin(),
                               resource.model.identifiers.end());
        for (auto &[candidate, position] : names)
            if (candidate != s.name && imports.contains(candidate))
                add(position, true);
        active.erase(s.name);
        done.insert(s.name);
        ordered.push_back(s);
    };
    for (size_t i = 0; i < p.schemas.size(); ++i)
        add(i, false);
    p.schemas = std::move(ordered);
    return p;
}
ProjectModel ProjectModel::dependencies(const std::filesystem::path &program) const {
    ProjectModel out = *this;
    const SourceEntry *selected = nullptr;
    for (auto &s : schemas)
        for (auto &c : s.classes)
            if (pathKey(c.file) == pathKey(program))
                selected = &c;
    for (auto &c : loose)
        if (pathKey(c.file) == pathKey(program))
            selected = &c;
    if (!selected)
        throw std::runtime_error("Selected source is not in the configured workspace: " +
                                 utf8(program.wstring()));
    auto dynamic = [](const SourceModel &model) {
        auto has = [&](const std::string &name) {
            return std::binary_search(model.identifiers.begin(), model.identifiers.end(), name);
        };
        for (auto &name : model.identifiers)
            if (name.starts_with("getClass") || name == "createInstance" ||
                name == "createObject" || name == "createTransientInstance" ||
                name == "createPersistentInstance")
                return true;
        return has("create") && has("as");
    };
    if (dynamic(selected->model))
        return out;
    for (auto &schema : schemas)
        for (auto &resource : schema.resources)
            if ((resource.file.filename() == L"importedPackageDefinitions.sec" ||
                 resource.file.filename() == L"exportedPackageDefinitions.sec") &&
                resource.model.identifiers.size() > 1)
                return out;
    std::map<std::string, const SourceEntry *> classes;
    std::map<std::string, std::string> parents;
    for (auto &s : schemas) {
        parents[s.name] = s.parent;
        for (auto &c : s.classes)
            if (!c.model.classes.empty())
                classes[s.name + "::" + c.model.classes[0].name] = &c;
    }
    for (auto &c : loose) {
        parents[c.model.schema] = c.model.parentSchema;
        for (auto &decl : c.model.classes)
            classes[c.model.schema + "::" + decl.name] = &c;
    }
    std::set<std::string> needed;
    std::vector<const SourceEntry *> queue{selected};
    auto namespaces = [&](std::string s) {
        std::vector<std::string> r;
        std::set<std::string> seen;
        while (!s.empty() && seen.insert(s).second) {
            r.push_back(s);
            s = parents.contains(s) ? parents.at(s) : "";
        }
        return r;
    };
    auto visible = namespaces(selected->model.schema);
    for (auto &s : schemas)
        if (std::find(visible.begin(), visible.end(), s.name) != visible.end()) {
            for (auto &c : s.classes) {
                if (c.model.classes.empty())
                    continue;
                auto parent = c.model.classes[0].parent;
                if (parent == "RootSchemaApp" || parent == "Application" ||
                    parent == "RootSchemaGlobal" || parent == "RootSchemaSession")
                    queue.push_back(&c);
            }
            for (auto &r : s.resources)
                queue.push_back(&r);
        }
    for (auto &c : loose)
        if (std::find(visible.begin(), visible.end(), c.model.schema) != visible.end())
            for (auto &decl : c.model.classes)
                if (decl.parent == "RootSchemaApp" || decl.parent == "Application" ||
                    decl.parent == "RootSchemaGlobal" || decl.parent == "RootSchemaSession")
                    queue.push_back(&c);
    for (size_t i = 0; i < queue.size(); ++i) {
        auto &e = *queue[i];
        if (!needed.insert(pathKey(e.file)).second)
            continue;
        auto ns = namespaces(e.model.schema.empty() ? selected->model.schema : e.model.schema);
        for (auto &identifier : e.model.identifiers)
            for (auto &space : ns) {
                auto found = classes.find(space + "::" + identifier);
                if (found != classes.end()) {
                    queue.push_back(found->second);
                    break;
                }
            }
    }
    for (auto &s : out.schemas)
        std::erase_if(s.classes, [&](auto &c) { return !needed.contains(pathKey(c.file)); });
    std::erase_if(out.loose, [&](auto &c) { return !needed.contains(pathKey(c.file)); });
    std::set<std::string> keep;
    for (auto &s : out.schemas)
        if (!s.classes.empty())
            for (auto &n : namespaces(s.name))
                keep.insert(n);
    std::erase_if(out.schemas, [&](auto &s) { return !keep.contains(s.name); });
    return out;
}
ProjectModel ProjectModel::select(const std::vector<std::filesystem::path> &files) const {
    if (files.empty() || !migrations.empty())
        return *this;
    std::set<std::string> wanted, schemaNames;
    for (auto &file : files) {
        auto extension = file.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".cls")
            return *this;
        auto scope = dependencies(file);
        for (auto &schema : scope.schemas) {
            schemaNames.insert(schema.name);
            for (auto &entry : schema.classes)
                wanted.insert(pathKey(entry.file));
        }
        for (auto &entry : scope.loose)
            wanted.insert(pathKey(entry.file));
    }
    ProjectModel selected = *this;
    std::erase_if(selected.schemas,
                  [&](auto &schema) { return !schemaNames.contains(schema.name); });
    for (auto &schema : selected.schemas)
        std::erase_if(schema.classes,
                      [&](auto &entry) { return !wanted.contains(pathKey(entry.file)); });
    std::erase_if(selected.loose,
                  [&](auto &entry) { return !wanted.contains(pathKey(entry.file)); });
    return selected;
}
json ProjectModel::inventory() const {
    if (!schemas.empty() && !loose.empty())
        throw std::runtime_error(
            "Missing schema header for " + loose.front().model.schema + " (" +
            utf8(loose.front().file.wstring()) +
            "). Include its .scm or schemaDefinitions.sec in the workspace or config.files.");
    json values = json::object();
    for (auto &s : schemas) {
        values[pathKey(s.header.file)] = {{"hash", s.header.hash},
                                          {"kind", "resource"},
                                          {"schema", s.name},
                                          {"parent", s.parent}};
        for (auto &e : s.resources)
            values[pathKey(e.file)] = {{"hash", e.hash}, {"kind", "resource"}, {"schema", s.name}};
        for (auto &e : s.classes)
            values[pathKey(e.file)] = {{"hash", e.hash},
                                       {"shape", e.shape},
                                       {"kind", "class"},
                                       {"schema", s.name},
                                       {"owner", e.model.classes[0].name}};
    }
    for (auto &e : loose)
        values[pathKey(e.file)] = {
            {"hash", e.hash},
            {"shape", e.shape},
            {"kind", "class"},
            {"schema", e.model.schema},
            {"owner", e.model.classes.empty() ? "" : e.model.classes[0].name}};
    for (auto &e : migrations)
        values[pathKey(e.file)] = {{"hash", e.hash}, {"kind", "migration"}};
    return values;
}
void ProjectModel::stage(const std::filesystem::path &destination) {
    stageDirectory = destination;
    std::filesystem::create_directories(destination);
    const std::vector<std::string> indexes = {
        "typeHeaders",     "interfaceDefs",          "membershipDefinitions",
        "typeDefinitions", "externalKeyDefinitions", "memberKeyDefinitions",
        "typeSources"};
    const std::vector<std::string> sections = {"schemaDefinitions",
                                               "localeDefinitions",
                                               "libraryDefinitions",
                                               "externalFunctions",
                                               "constantDefinitions",
                                               "translatableStringDefinitions",
                                               "typeHeaders",
                                               "interfaceDefs",
                                               "membershipDefinitions",
                                               "typeDefinitions",
                                               "externalKeyDefinitions",
                                               "memberKeyDefinitions",
                                               "typeSources",
                                               "inverseDefinitions",
                                               "databaseDefinitions",
                                               "exposedListDefinitions",
                                               "exportedPackageDefinitions",
                                               "importedPackageDefinitions",
                                               "externalFunctionSources"};
    for (size_t i = 0; i < schemas.size(); ++i) {
        auto &s = schemas[i];
        auto shortName = "s" + std::to_string(i);
        s.directory = destination / shortName;
        s.joined = destination / (shortName + ".scm");
        auto types = s.directory / L"Types";
        std::filesystem::create_directories(types);
        for (auto &name : sections)
            writeText(s.directory / (name + ".sec"), "");
        for (auto &e : s.resources)
            if (e.extension == ".sec")
                writeText(s.directory / e.file.filename(), "\xEF\xBB\xBF" + e.text);
        {
            std::istringstream input(s.header.text);
            std::string line, normalized;
            while (std::getline(input, line)) {
                if (line.ends_with('\r'))
                    line.pop_back();
                if (!line.empty() && !std::isspace(static_cast<unsigned char>(line[0])) &&
                    !line.starts_with("jadeVersionNumber") && line != "schemaDefinition")
                    normalized += '\t';
                normalized += line + "\r\n";
            }
            writeText(s.directory / L"schemaDefinitions.sec", "\xEF\xBB\xBF" + normalized);
        }
        std::map<std::string, std::vector<std::string>> owners;
        for (auto &e : s.classes) {
            auto text = e.text;
            if (!section(e.model.code, "databaseDefinitions")) {
                auto pos = sectionStart(e.model.code, "typeSources");
                if (pos != std::string::npos)
                    text.insert(pos, "databaseDefinitions\r\n\r\n");
                else
                    text += "\r\ndatabaseDefinitions\r\n\r\ntypeSources\r\n";
            }
            auto code = SourceModel::parse(text, "").code;
            std::istringstream lines(text), lex(code);
            std::string line, masked, normalized;
            bool inSources = false;
            while (std::getline(lines, line)) {
                std::getline(lex, masked);
                if (line.ends_with('\r'))
                    line.pop_back();
                if (masked.ends_with('\r'))
                    masked.pop_back();
                auto name = masked;
                auto first = name.find_first_not_of(" \t");
                auto last = name.find_last_not_of(" \t");
                name = first == std::string::npos ? std::string()
                                                  : name.substr(first, last - first + 1);
                if (name == "typeSources")
                    inSources = true;
                bool top = name.starts_with("jadeVersionNumber") || name == "schemaDefinition" ||
                           std::find(indexes.begin(), indexes.end(), name) != indexes.end() ||
                           name == "databaseDefinitions";
                if (!inSources && !top && !masked.empty() &&
                    !std::isspace(static_cast<unsigned char>(masked[0])))
                    normalized += '\t';
                normalized += line + "\r\n";
            }
            auto owner = e.model.classes.at(0).name;
            writeText(types / (owner + ".cls"), "\xEF\xBB\xBF" + normalized);
            for (auto &name : indexes)
                if (section(e.model.code, name))
                    owners[name].push_back(owner);
        }
        for (auto &name : indexes) {
            std::sort(owners[name].begin(), owners[name].end());
            std::string text = "\xEF\xBB\xBF";
            for (auto &owner : owners[name])
                text += owner + "\r\n";
            writeText(s.directory / (name + ".sec"), text);
        }
        std::vector<std::filesystem::path> ddxFiles;
        for (auto &resource : s.resources)
            if (resource.extension == ".ddx")
                ddxFiles.push_back(resource.file);
        if (!ddxFiles.empty()) {
            s.ddx = destination / (shortName + ".ddx");
            writeText(s.ddx, DdxMerger::merge(s.name, ddxFiles));
        }
    }
}
} // namespace jade::native
