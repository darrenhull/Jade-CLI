#include "ProjectModel.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace jade::native;
int main() {
    ProjectModel project;
    for (const auto &[name, parent] : std::vector<std::pair<std::string, std::string>>{
             {"Base", "RootSchema"}, {"Empty", "Base"}, {"Other", "RootSchema"}}) {
        ProjectSchema schema;
        schema.name = name;
        schema.parent = parent;
        schema.header.file = std::filesystem::path(name + ".scm");
        schema.header.text =
            "schemaDefinition\n" + name + " subschemaOf " + parent + " partialDefinition;\n";
        project.schemas.push_back(std::move(schema));
    }
    SourceEntry locale;
    locale.file = "localeDefinitions.sec";
    locale.extension = ".sec";
    project.schemas[1].resources.push_back(locale);
    auto empty = project.schemaScope("Empty");
    assert(empty.schemas.size() == 2 && empty.schemas[1].name == "Empty");
    assert(empty.schemas[1].classes.empty() && empty.schemas[1].resources.size() == 1);
    assert(empty.scopeSchemas.contains("Empty") && !empty.scopeSchemas.contains("Other"));
    for (int i = 0; i < 30000; ++i) {
        SourceEntry entry;
        entry.file = std::filesystem::path("C" + std::to_string(i) + ".cls");
        entry.model.schema = "Empty";
        entry.model.classes.push_back({"C" + std::to_string(i), "Object", false});
        project.schemas[1].classes.push_back(std::move(entry));
    }
    const auto start = std::chrono::steady_clock::now();
    auto large = project.schemaScope("Empty");
    assert(large.schemas.size() == 2 && large.schemas[1].classes.size() == 30000);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    assert(elapsed < 5000); // Whole-scope copying, not dependency scans for every class.
    SourceEntry migration;
    migration.file = "rename.jcf";
    migration.extension = ".jcf";
    migration.text =
        "JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nRename Class Other::Before After\n";
    project.migrations.push_back(migration);
    assert(project.schemaScope("Empty").migrations.empty());
    project.migrations[0].text =
        "JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nRename Class Empty::Before After\n";
    const auto scopedMigration = project.schemaScope("Empty");
    assert(scopedMigration.migrations.size() == 1 &&
           scopedMigration.migrations[0].model.schema == "Empty");
    project.migrations[0].text += "Rename Class Other::Before After\n";
    bool mixed = false;
    try {
        project.schemaScope("Empty");
    } catch (const std::exception &e) {
        mixed = std::string(e.what()).find("crosses") != std::string::npos;
    }
    assert(mixed);
    std::cout << "One-pass 30,000-class schema scope retained resources and empty headers: "
              << elapsed << " ms\n";
}
