#include "WorkspaceRunner.hpp"
#include "../fixtures/DisposableWorkingDirectory.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace jade::native;
int main() try {
    const auto *seedValue = std::getenv("JADE_CPP_WORKSPACE_TEST_SEED");
    if (!seedValue) {
        std::cout << "SKIP: set JADE_CPP_WORKSPACE_TEST_SEED to a private TEMP seed\n";
        return 0;
    }
    const auto seed = std::filesystem::path(wide(seedValue));
    if (!contains(std::filesystem::temp_directory_path(), seed) ||
        !std::filesystem::is_regular_file(seed / "_control.dat"))
        throw std::runtime_error("Private TEMP seed required.");
    DisposableWorkingDirectory fixture("jade-workspace-scope-proof-");
    const auto root = fixture.path(), src = root / "src";
    const auto suffix = uniqueId().substr(0, 8), a = "WorkspaceEmpty" + suffix,
               b = "WorkspaceOther" + suffix;
    auto header = [](const std::string &schema) {
        return "jadeVersionNumber \"25.0.02\";\nschemaDefinition\n" + schema +
               " subschemaOf RootSchema partialDefinition;\n";
    };
    auto cls = [&](const std::string &schema, const std::string &owner) {
        return header(schema) + "typeHeaders\n" + owner +
               " subclassOf Object transient;\ntypeDefinitions\n" + owner +
               " completeDefinition\n(\n)\ntypeSources\n";
    };
    auto emptySchema = [&](const std::string &schema) {
        return header(schema) + "typeHeaders\n" + schema +
               " subclassOf RootSchemaApp transient;\nG" + schema +
               " subclassOf RootSchemaGlobal transient;\nS" + schema +
               " subclassOf RootSchemaSession transient;\ntypeDefinitions\n" + schema +
               " completeDefinition\n(\n)\nG" + schema + " completeDefinition\n(\n)\nS" + schema +
               " completeDefinition\n(\n)\ndatabaseDefinitions\n" + schema +
               "Db\n(\ndatabaseFileDefinitions\n\"" + schema + "\";\ndefaultFileDefinition \"" +
               schema + "\";\nclassMapDefinitions\n" + schema + " in \"_usergui\";\nG" + schema +
               " in \"" + schema + "\";\nS" + schema + " in \"_environ\";\n)\ntypeSources\n";
    };
    writeText(src / (a + ".scm"), emptySchema(a));
    json config = {{"installPath", "C:/Jade2025"},
                   {"databasePath", utf8(seed.wstring())},
                   {"iniPath", utf8((seed.parent_path() / "working.ini").wstring())},
                   {"schema", a},
                   {"workspaceRoots", {utf8(src.wstring())}},
                   {"storagePath", utf8((root / "private").wstring())},
                   {"server", "singleUser"},
                   {"timeoutMilliseconds", 60000}};
    WorkingEngine working(std::filesystem::current_path());
    WorkspaceRunner runner(working);
    std::string log;
    auto run = [&](const std::string &text) {
        log.clear();
        return runner.execute(
            {{"config", config}, {"sourceText", text}, {"source", "untitled:scoped-proof"}},
            [&](const auto &line) {
                std::cerr << line;
                log += line;
            });
    };
    try {
        const auto empty = run("write 42;");
        std::cout << "Header-only schema: " << empty.dump() << '\n';
        if (!empty.value("passed", false))
            throw std::runtime_error("Header-only schema was not loaded: " + empty.dump());
        // Exercise the standard native extracted layout before combining
        // a minimal schema header with independently edited section resources.
        auto splitScaffold = [&](const std::string &schema) {
            writeText(src / (schema + ".scm"), header(schema));
            const auto document = emptySchema(schema);
            const auto databaseStart = document.find("databaseDefinitions");
            writeText(src / schema / "databaseDefinitions.sec",
                      document.substr(databaseStart,
                                      document.find("typeSources", databaseStart) - databaseStart));
            for (const auto &[owner, parent, map] :
                 std::vector<std::tuple<std::string, std::string, std::string>>{
                     {schema, "RootSchemaApp", "_usergui"},
                     {"G" + schema, "RootSchemaGlobal", schema},
                     {"S" + schema, "RootSchemaSession", "_environ"}}) {
                auto text = header(schema) + "typeHeaders\n" + owner + " subclassOf " + parent +
                            " transient;\ntypeDefinitions\n" + owner +
                            " completeDefinition\n(\n)\ndatabaseDefinitions\n" + schema +
                            "Db\n(\ndatabaseFileDefinitions\n\"" + schema +
                            "\";\ndefaultFileDefinition \"" + schema +
                            "\";\nclassMapDefinitions\n" + owner + " in \"" + map +
                            "\";\n)\ntypeSources\n";
                writeText(src / schema / "types" / (owner + ".cls"), text);
            }
        };
        splitScaffold(a);
        const auto split = run("write 42;");
        if (!split.value("passed", false))
            throw std::runtime_error("Native split scaffold failed: " + split.dump());
        const auto resource = src / a / "localeDefinitions.sec";
        writeText(resource,
                  "localeDefinitions\n\t1033 \"English (United States)\" schemaDefaultLocale;\n");
        // Native schema loads deliberately do not add or modify locales:
        // JADE2025/OnlineDocumentation/.../ch11localization/loading_locales.htm.
        // Verify the edited section reaches native join/load, not invented locale semantics.
        const auto localeQuery = "write 42;";
        const auto locale = run(localeQuery);
        bool stagedLocale = false;
        const auto assembly =
            std::filesystem::path(wide(WorkingEngine::workingLocation(config).at("assemblyRoot")));
        for (const auto &entry : std::filesystem::recursive_directory_iterator(assembly))
            if (entry.path().filename() == L"localeDefinitions.sec" &&
                readText(entry.path()).find("English (United States)") != std::string::npos)
                stagedLocale = true;
        if (!locale.value("passed", false) || !stagedLocale ||
            log.find("Rejoining native schema") == std::string::npos)
            throw std::runtime_error(
                "Resource-only section was not supplied to native join/load: " + locale.dump());
        const auto unchanged = run(localeQuery);
        if (!unchanged.value("passed", false) ||
            log.find("Native source inputs are already loaded.") == std::string::npos ||
            log.find("Rejoining native schema") != std::string::npos)
            throw std::runtime_error("Unchanged header/resource snapshot was loaded again.");
        writeText(src / (b + ".scm"), emptySchema(b));
        config["schema"] = b;
        const auto otherSchema = run("write 73;");
        if (!otherSchema.value("passed", false))
            throw std::runtime_error("Second native schema scaffold failed: " + otherSchema.dump());
        splitScaffold(b);
        writeText(src / b / "types" / "KeepType.cls", cls(b, "KeepType"));
        config["schema"] = b;
        const auto other =
            run("vars x:KeepType; begin create x transient; delete x; write 73; end;");
        if (!other.value("passed", false))
            throw std::runtime_error("Unrelated schema load failed: " + other.dump());
        const auto stateFile =
            std::filesystem::path(wide(WorkingEngine::workingLocation(config).at("root"))) /
            "native-inputs-v1.json";
        const auto before = json::parse(readText(stateFile));
        const auto removed = src / a / "types" / "RemovedType.cls";
        writeText(removed, cls(a, "RemovedType"));
        config["schema"] = a;
        const auto loaded = run("vars x:RemovedType; begin create x transient; delete x; end;");
        if (!loaded.value("passed", false))
            throw std::runtime_error("Local class creation failed: " + loaded.dump());
        std::filesystem::remove(removed);
        const auto absent = run("vars x:RemovedType; begin end;");
        if (absent.value("passed", true) || absent.value("code", 0) != 6023)
            throw std::runtime_error("Deleted local class remained executable: " + absent.dump());
        const auto after = json::parse(readText(stateFile));
        for (const auto &[key, value] : before.items())
            if (value.value("schema", "") == b && (!after.contains(key) || after[key] != value))
                throw std::runtime_error("Unrelated schema inventory was changed.");
        config["schema"] = b;
        const auto preserved =
            run("vars x:KeepType; begin create x transient; delete x; write 73; end;");
        if (!preserved.value("passed", false) || preserved["report"]["output"] != "73\n")
            throw std::runtime_error("Unrelated schema was deleted: " + preserved.dump());
        std::filesystem::remove(src / (a + ".scm"));
        config["schema"] = a;
        const auto missingHeader = run("write 42;");
        if (missingHeader.value("passed", true) ||
            missingHeader.at("report").value("error", "").find("managed schema header") ==
                std::string::npos)
            throw std::runtime_error("Missing managed schema header did not fail closed: " +
                                     missingHeader.dump());
        config["schema"] = b;
        const auto afterMissingHeader =
            run("vars x:KeepType; begin create x transient; delete x; write 73; end;");
        if (!afterMissingHeader.value("passed", false) ||
            afterMissingHeader.at("report").at("output") != "73\n")
            throw std::runtime_error("Missing header affected an unrelated schema: " +
                                     afterMissingHeader.dump());
        working.disconnect();
        fixture.remove();
        std::cout << "Header/resource-only prep, filesystem deletion and unrelated schema native "
                     "proof passed\n";
    } catch (...) {
        auto original = std::current_exception();
        try {
            working.disconnect();
        } catch (const std::exception &e) {
            std::cerr << "Native cleanup: " << e.what() << '\n';
        }
        try {
            fixture.remove();
        } catch (const std::exception &e) {
            std::cerr << "Fixture cleanup: " << e.what() << '\n';
        }
        std::rethrow_exception(original);
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
