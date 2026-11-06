#include "WorkingEngine.hpp"
#include "NativeReports.hpp"
#include "NativeMigrations.hpp"
#include "PrivateFile.hpp"
#include "InputInventory.hpp"
#include "NativeTimeout.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
namespace jade::native {
static std::filesystem::path pathOf(const json &j, const std::string &key) {
    return std::filesystem::path(wide(j.at(key).get<std::string>()));
}
static std::string strPath(const std::filesystem::path &p) {
    return utf8(p.wstring());
}
static json readJson(const std::filesystem::path &file, json defaultValue = json::object()) {
    return std::filesystem::exists(file) ? json::parse(readText(file)) : defaultValue;
}
static unsigned timeoutOf(const json &config) {
    return config.value("timeoutMilliseconds", config.value("testTimeoutSeconds", 120U) * 1000U);
}
json WorkingEngine::workingLocation(const json &config) {
    auto source = std::filesystem::absolute(pathOf(config, "databasePath")),
         install = std::filesystem::absolute(pathOf(config, "installPath")),
         ini = !config.value("iniPath", std::string()).empty() ? pathOf(config, "iniPath")
                                                               : source / L"jade.ini";
    json roots = json::array();
    for (auto &r : config.value("workspaceRoots", json::array()))
        roots.push_back(pathKey(wide(r.get<std::string>())));
    auto identity =
        sha256(json::array({"native-v1", pathKey(source), pathKey(install), pathKey(ini), roots,
                            config.value("workingDatabaseGeneration", std::string()).empty()
                                ? "initial"
                                : config.at("workingDatabaseGeneration").get<std::string>()})
                   .dump())
            .substr(0, 24);
    auto root = !config.value("workingDatabaseDirectory", std::string()).empty()
                    ? std::filesystem::absolute(pathOf(config, "workingDatabaseDirectory"))
                    : pathOf(config, "storagePath") / L"working-databases" / identity;
    auto assembly =
        std::filesystem::temp_directory_path() / ("jd-" + sha256(pathKey(root)).substr(0, 8));
    return {{"root", strPath(root)},
            {"databasePath", strPath(root / L"system")},
            {"iniPath", strPath(root / L"working.ini")},
            {"installPath", strPath(install)},
            {"sourceDatabasePath", strPath(source)},
            {"identity", identity},
            {"assemblyRoot", strPath(assembly)}};
}
json WorkingEngine::inspectionConfig(const json &config) {
    auto location = workingLocation(config);
    auto root = pathOf(location, "root");
    auto marker = readJson(root / L"working-database.json");
    if (marker.value("identity", "") != location.at("identity").get<std::string>() ||
        !std::filesystem::is_regular_file(pathOf(location, "databasePath") / L"_control.dat"))
        throw std::runtime_error("Build the workspace first to prepare a valid Working database.");
    auto result = config;
    result.update(location);
    result["server"] = "singleUser";
    result["readOnly"] = true;
    return result;
}
std::string WorkingEngine::isolatedIni(const std::string &text, const std::filesystem::path &root) {
    using Values = std::map<std::string, std::string>;
    std::map<std::string, std::pair<std::string, Values>> pending;
    auto add = [&](std::string section, std::string key, std::string value) {
        auto lower = section;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        pending[lower].first = section;
        pending[lower].second[key] = value;
    };
    auto folder = [&](const char *s) { return strPath(root / wide(s)); };
    add("Jade", "Server", "SingleUser");
    add("NonGuiClient", "Server", "SingleUser");
    add("JadeLog", "LogDirectory", folder("logs"));
    add("PersistentDb", "ActivityLogDirectory", folder("logs"));
    add("PersistentDb", "JournalRootDirectory", folder("journals"));
    add("TransientDb", "FileDirectory", folder("transient"));
    add("TransientDb", "ActivityLogDirectory", folder("logs"));
    add("JadeEnvironment", "JadeWorkDirectory", folder("scratch"));
    add("JadeEnvironment", "ProgramDataDirectory", folder("environment"));
    add("FaultHandling", "CrashLogDirectory", folder("logs"));
    add("FaultHandling", "ProcessDumpDirectory", folder("logs"));
    add("JadeMonitorBackground", "DefaultLogDirectory", folder("logs"));
    std::string current, result, line;
    std::istringstream stream(text);
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    auto finish = [&] {
        if (pending.contains(current)) {
            for (auto &[key, value] : pending[current].second)
                result += key + "=" + value + "\r\n";
            pending.erase(current);
        }
    };
    std::regex section(R"(^\s*\[([^\]]+)\])"), key(R"(^\s*([^;#=]+?)\s*=)");
    while (std::getline(stream, line)) {
        if (line.ends_with('\r'))
            line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, section)) {
            finish();
            current = lower(m[1]);
            result += line + "\r\n";
            continue;
        }
        if (pending.contains(current) && std::regex_search(line, m, key)) {
            auto &values = pending[current].second;
            auto found = std::find_if(values.begin(), values.end(),
                                      [&](auto &item) { return lower(item.first) == lower(m[1]); });
            if (found != values.end()) {
                result += found->first + "=" + found->second + "\r\n";
                values.erase(found);
                continue;
            }
        }
        result += line + "\r\n";
    }
    finish();
    for (auto &[name, entry] : pending) {
        result += "[" + entry.first + "]\r\n";
        for (auto &[key, value] : entry.second)
            result += key + "=" + value + "\r\n";
    }
    return result;
}
json WorkingEngine::prepare(const json &config, Output output, std::atomic_bool *cancelled) {
    auto working = workingLocation(config);
    auto root = pathOf(working, "root"), source = pathOf(working, "sourceDatabasePath"),
         install = pathOf(working, "installPath");
    if (contains(source, root) || contains(root, source))
        throw std::runtime_error("Working database must be separate from the configured target "
                                 "database and workspace source.");
    for (auto &value : config.value("workspaceRoots", json::array())) {
        std::filesystem::path workspace(wide(value.get<std::string>()));
        if (contains(workspace, root) || contains(root, workspace))
            throw std::runtime_error("Working database must be separate from the configured target "
                                     "database and workspace source.");
    }
    bool online = config.value("server", "singleUser") == "multiUser";
    for (auto &name : {L"jdbutilb.exe", L"jadloadb.exe", L"jadclient.exe"})
        if (!std::filesystem::is_regular_file(install / L"bin" / name))
            throw std::runtime_error("Native Jade tool not found: " + utf8(name));
    if (online && !std::filesystem::is_regular_file(install / L"bin" / L"jdbadmin.exe"))
        throw std::runtime_error("Native Jade tool not found: jdbadmin.exe");
    if (!std::filesystem::is_regular_file(source / L"_control.dat"))
        throw std::runtime_error("Configured database has no _control.dat.");
    auto marker = root / L"working-database.json";
    if (std::filesystem::exists(marker)) {
        auto saved = readJson(marker);
        if (saved.value("identity", "") != working["identity"].get<std::string>() ||
            !std::filesystem::is_regular_file(pathOf(working, "databasePath") / L"_control.dat"))
            throw std::runtime_error(
                "Working database is incomplete or belongs to another target.");
        return working;
    }
    if (std::filesystem::exists(pathOf(working, "databasePath") / L"_control.dat"))
        throw std::runtime_error(
            "Unmanaged working database exists; select a new empty directory.");
    PrivateFile::protectDirectory(root);
    for (auto &name :
         {L"logs", L"journals", L"transient", L"scratch", L"environment", L"backup", L"system"})
        std::filesystem::create_directories(root / name);
    auto sourceIni = !config.value("iniPath", std::string()).empty() ? pathOf(config, "iniPath")
                                                                     : source / L"jade.ini";
    writeText(pathOf(working, "iniPath"), isolatedIni(readText(sourceIni), root));
    auto backupIni = root / L"source-backup.ini";
    if (online)
        std::filesystem::copy_file(sourceIni, backupIni,
                                   std::filesystem::copy_options::overwrite_existing);
    if (output)
        output(online ? "Creating a native online quiesced backup for the working database ...\n"
                      : "Creating a consistent native backup for the working database ...\n");
    session_.disconnect();
    std::vector<std::wstring> args =
        online ? std::vector<std::wstring>{L"action=Backup",
                                           L"path=" + source.wstring(),
                                           L"ini=" + backupIni.wstring(),
                                           L"server=multiUser",
                                           L"backupDir=" + (root / L"backup").wstring(),
                                           L"quiesced=true",
                                           L"verify=true",
                                           L"showProgress=true"}
               : std::vector<std::wstring>{L"path=" + source.wstring(),
                                           L"ini=" + pathOf(working, "iniPath").wstring(),
                                           L"backup",
                                           L"backupDir=" + (root / L"backup").wstring(),
                                           L"verify",
                                           L"nostatus"};
    NativeProcess backup;
    auto result = backup.run(install / L"bin" / (online ? L"jdbadmin.exe" : L"jdbutilb.exe"), args,
                             root, cancelled, timeoutOf(config), output);
    if (result.timedOut)
        throw NativeTimeout("Native database backup timed out.");
    if (!result.success())
        throw std::runtime_error("Native database backup failed (exit " +
                                 std::to_string(result.code) + "). " + result.output);
    if (output)
        output("Restoring the isolated working database ...\n");
    NativeProcess restore;
    result = restore.run(install / L"bin" / L"jdbutilb.exe",
                         {L"path=" + pathOf(working, "databasePath").wstring(),
                          L"ini=" + pathOf(working, "iniPath").wstring(), L"restore",
                          L"backupDir=" + (root / L"backup").wstring(), L"noRecovery",
                          L"clearFilePaths", L"verify", L"nostatus"},
                         root, cancelled, timeoutOf(config), output);
    if (result.timedOut)
        throw NativeTimeout("Native working database restore timed out.");
    if (!result.success() ||
        !std::filesystem::exists(pathOf(working, "databasePath") / L"_control.dat"))
        throw std::runtime_error("Native working database restore failed (exit " +
                                 std::to_string(result.code) + "). " + result.output);
    writeText(marker, working.dump(2));
    return working;
}
json WorkingEngine::load(const json &working, const std::vector<std::wstring> &args, Output output,
                         std::atomic_bool *cancelled) {
    auto helper = distributionRoot_ / L"native" / L"jade-test-helper.exe";
    if (!std::filesystem::is_regular_file(helper))
        helper = distributionRoot_ / L"native-bin" / L"jade-test-helper.exe";
    std::vector<std::string> converted;
    for (auto &a : args)
        converted.push_back(utf8(a));
    return session_.request(helper, working, "load", {{"args", converted}}, output, cancelled,
                            working.value("timeoutMilliseconds", 120000U));
}
static std::string xml(std::string s) {
    std::string r;
    for (auto c : s)
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        default:
            r += c;
        }
    return r;
}
json WorkingEngine::execute(const json &request, Output output, std::atomic_bool *cancelled) {
    json report = {{"compiled", false}, {"success", false}, {"diagnostics", json::array()}};
    try {
        auto config = request.at("config");
        auto program = pathOf(config, "program");
        report["source"] = strPath(program);
        auto working = prepare(config, output, cancelled);
        working["timeoutMilliseconds"] = timeoutOf(config);
        for (auto &key : {"user", "password"})
            if (config.contains(key))
                working[key] = config[key];
        auto root = pathOf(working, "root");
        report["workingDatabase"] = working["databasePath"];
        report["databaseNeverOpened"] = false;
        if (output)
            output("Using isolated working database " + working["databasePath"].get<std::string>() +
                   "\nConfigured publish target: " + config["databasePath"].get<std::string>() +
                   "\n");
        if (cancelled && cancelled->load())
            throw std::runtime_error("Operation cancelled.");
        std::vector<std::filesystem::path> roots;
        for (auto &value :
             config.value("workspaceRoots", json::array({strPath(program.parent_path())})))
            roots.emplace_back(wide(value.get<std::string>()));
        std::vector<std::filesystem::path> explicitFiles;
        for (auto &file : config.value("files", json::array()))
            explicitFiles.emplace_back(wide(file.get<std::string>()));
        auto project = ProjectModel::scan(roots, explicitFiles);
        bool test = request.at("command") == "test" || request.at("command") == "test.run";
        bool fullBuild = !test && config.value("projectBuild", false);
        if (test && config.value("testDependencyScope", "dependencies") != "workspace")
            project = project.dependencies(program);
        else if (!test && !fullBuild) {
            std::vector<std::filesystem::path> selected;
            for (auto &file : config.value("selectedFiles", json::array({strPath(program)})))
                selected.emplace_back(wide(file.get<std::string>()));
            project = project.select(selected);
        }
        for (auto &schema : project.schemas)
            for (auto &entry : schema.classes)
                if (entry.model.classes.size() != 1)
                    throw std::runtime_error("Expected one class/type per CLS: " +
                                             strPath(entry.file));
        auto inventory = project.inventory();
        auto stateFile = root / L"native-inputs-v1.json";
        auto state = readJson(stateFile);
        std::vector<SourceEntry> sources;
        for (auto &s : project.schemas) {
            sources.push_back(s.header);
            sources.insert(sources.end(), s.resources.begin(), s.resources.end());
            sources.insert(sources.end(), s.classes.begin(), s.classes.end());
        }
        sources.insert(sources.end(), project.loose.begin(), project.loose.end());
        auto plan = InputInventory::compare(inventory, state, fullBuild);
        bool changed = plan.changed, structural = plan.structural;
        std::vector<SourceEntry> delta;
        std::set<std::string> changedKeys(plan.deltaKeys.begin(), plan.deltaKeys.end());
        for (auto &source : sources)
            if (changedKeys.contains(pathKey(source.file)))
                delta.push_back(source);
        if (changed) {
            writeText(stateFile, "{}");
            json loaded;
            std::filesystem::path staging =
                pathOf(working, "assemblyRoot") / uniqueId().substr(0, 8);
            if (structural && !project.schemas.empty()) {
                session_.disconnect();
                NativeMigrations::apply(project, working, distributionRoot_, output, cancelled);
                project.stage(staging);
                for (auto &s : project.schemas) {
                    if (output)
                        output("Rejoining native schema " + s.name + " ...\n");
                    NativeProcess join;
                    auto joined = join.run(
                        pathOf(working, "installPath") / L"bin" / L"jadclient.exe",
                        {L"path=" + pathOf(working, "databasePath").wstring(),
                         L"ini=" + pathOf(working, "iniPath").wstring(), L"server=singleUser",
                         L"schema=JadeSchema", L"app=JadeJoinSchema", L"startAppParameters",
                         s.directory.wstring(), s.joined.wstring(), L"endAppParameters"},
                        staging, cancelled, timeoutOf(config), output);
                    if (joined.timedOut)
                        throw NativeTimeout("Native schema rejoin timed out for " + s.name + ".");
                    if (!joined.success() || !std::filesystem::is_regular_file(s.joined))
                        throw std::runtime_error("Native schema rejoin failed for " + s.name +
                                                 ". " + joined.output);
                    auto assembled = SourceModel::parse(readText(s.joined), strPath(s.joined));
                    std::set<std::string> names;
                    for (auto &c : assembled.classes)
                        names.insert(c.name);
                    for (auto &c : s.classes)
                        if (!names.contains(c.model.classes[0].name))
                            throw std::runtime_error("Native join omitted " + s.name +
                                                     "::" + c.model.classes[0].name);
                }
                auto batch = staging / L"workspace.mul";
                std::string mul = "#MULTIPLE_SCHEMA_EXTRACT\r\n";
                for (auto &s : project.schemas)
                    mul += strPath(s.joined.filename()) +
                           (s.ddx.empty() ? "" : " " + strPath(s.ddx.filename())) + "\r\n";
                writeText(batch, mul);
                for (auto &schema : plan.removedSchemas) {
                    if (output)
                        output("Removing deleted workspace schema " + schema +
                               " from Working ...\n");
                    auto result =
                        load(working, {L"deleteSchema=" + wide(schema)}, output, cancelled);
                    if (!result.value("success", false))
                        throw std::runtime_error("Native schema deletion failed for " + schema +
                                                 " (exit " +
                                                 std::to_string(result.value("code", 2)) + ").");
                }
                loaded = load(working,
                              {L"schemaFile=" + batch.wstring(), L"loadStyle=currentSchemaVersion",
                               L"replayableReorg=true", L"dontSaveSources=false",
                               L"compileUnchangedMethods=true", L"allowCircularPackages=true",
                               std::wstring(L"deleteIfAbsent=") + (fullBuild ? L"true" : L"false"),
                               L"ShowProgress=true"},
                              output, cancelled);
                report["native_project"] = true;
                report["loaded_files"] = inventory.size();
            } else {
                auto directory = root / L"loads" / uniqueId();
                std::filesystem::create_directories(directory);
                std::string mul = "#MULTIPLE_SCHEMA_EXTRACT\r\n";
                size_t index = 0;
                for (auto &e : delta) {
                    if (e.extension != ".cls")
                        continue;
                    auto target = directory / (std::to_string(index++) + ".cls");
                    stageSourceFile(e, target);
                    mul += strPath(target.filename()) + "\r\n";
                    if (output)
                        output("Loading and compiling " + strPath(e.file) +
                               " in the working database ...\n");
                }
                if (!index)
                    throw std::runtime_error("No compilable native class inputs found.");
                auto batch = directory / L"sources.mul";
                writeText(batch, mul);
                loaded = load(working,
                              {L"schemaFile=" + batch.wstring(), L"loadStyle=currentSchemaVersion",
                               L"replayableReorg=true", L"dontSaveSources=false",
                               L"compileUnchangedMethods=true", L"ShowProgress=true"},
                              output, cancelled);
                report["loaded_files"] = index;
                report["incremental"] = !structural;
            }
            auto nativeOutput = loaded.value("output", "");
            report["diagnostics"] = NativeReports::diagnostics(nativeOutput, sources);
            if (!report["diagnostics"].empty()) {
                loaded["success"] = false;
                if (loaded.value("code", 0) == 0)
                    loaded["code"] = report["diagnostics"][0].value("compiler_error", 1);
            }
            if (!loaded.value("success", false) || loaded.value("code", 2) != 0) {
                report["error"] = "Native schema load failed (exit " +
                                  std::to_string(loaded.value("code", 2)) + "). See native output.";
                return {{"passed", false}, {"code", loaded.value("code", 2)}, {"report", report}};
            }
            auto combined = state;
            for (auto &[key, value] : inventory.items())
                combined[key] = value;
            writeText(stateFile, (fullBuild ? inventory : combined).dump(2));
        } else {
            report["native_source_reused"] = true;
            report["loaded_files"] = 0;
            if (output)
                output("Native source inputs are already loaded.\n");
        }
        report["compiled"] = true;
        report["success"] = true;
        if (!test)
            return {{"passed", true},
                    {"code", 0},
                    {"report", report},
                    {"cancelled", false},
                    {"timedOut", false}};
        auto model = SourceModel::parse(readText(program), strPath(program));
        if (model.classes.size() != 1)
            throw std::runtime_error("Select a CLS containing one test class.");
        std::vector<std::string> methods;
        if (config.contains("methods")) {
            if (config["methods"].is_array())
                methods = config["methods"].get<std::vector<std::string>>();
            else
                methods.push_back(config["methods"]);
        }
        auto directory = root / L"runs" / uniqueId();
        auto control = directory / L"tests.xml", log = directory / L"results.log";
        std::string selected;
        for (auto &m : methods)
            selected += "<method name=\"" + xml(m) + "\"/>";
        writeText(control,
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<unitTest><reporter logFile=\"" +
                      xml(strPath(log)) +
                      "\"/><options logCallStack=\"true\"/><tests><test "
                      "name=\"WorkspaceTests\"><schema name=\"" +
                      xml(model.schema) + "\"/><class name=\"" + xml(model.classes[0].name) +
                      "\">" + selected + "</class></test></tests></unitTest>");
        auto helper = distributionRoot_ / L"native" / L"jade-test-helper.exe";
        if (!std::filesystem::is_regular_file(helper))
            helper = distributionRoot_ / L"native-bin" / L"jade-test-helper.exe";
        auto result = session_.request(helper, working, "run",
                                       {{"controlFile", strPath(control)},
                                        {"testName", "WorkspaceTests"},
                                        {"logFile", strPath(log)}},
                                       output, cancelled, timeoutOf(config));
        auto contents = std::filesystem::exists(log) ? readText(log) : "";
        if (output && !contents.empty())
            output(contents);
        auto tested = NativeReports::tests(contents, model, methods);
        report.update(tested);
        report["logFile"] = strPath(log);
        bool success = tested.value("tests_completed", false) &&
                       tested.value("failures", json(0)) == 0 && result.value("success", false);
        report["success"] = success;
        for (auto &row : tested["results"])
            if (output)
                output(row["status"].get<std::string>() + ": " + row["owner"].get<std::string>() +
                       "::" + row["method"].get<std::string>() +
                       (row.contains("message") ? " - " + row["message"].get<std::string>() : "") +
                       "\n");
        return {{"passed", success},
                {"code", success ? 0 : 1},
                {"report", report},
                {"cancelled", false},
                {"timedOut", false}};
    } catch (const std::exception &error) {
        report["compiled"] = false;
        report["success"] = false;
        report["error"] = error.what();
        bool aborted = cancelled && cancelled->load();
        bool timedOut = dynamic_cast<const NativeTimeout *>(&error) != nullptr;
        return {{"passed", false},
                {"code", aborted ? 130 : 2},
                {"cancelled", aborted},
                {"timedOut", timedOut},
                {"report", report}};
    }
}
} // namespace jade::native
