#include "ProjectModel.hpp"
#include "InputInventory.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    ProjectModel project;
    ProjectSchema local;
    local.name = "A";
    local.parent = "RootSchema";
    local.header.file = "A.scm";
    SourceEntry imported;
    imported.file = "importedPackageDefinitions.sec";
    imported.model = SourceModel::parse("importedPackageDefinitions\nBPackage from B;\n", "");
    local.resources.push_back(imported);
    project.schemas.push_back(local);
    SourceEntry dependency;
    dependency.file = "B.cls";
    dependency.model.schema = "B";
    dependency.model.parentSchema = "RootSchema";
    dependency.model.classes.push_back({"LocalB", "Object", false});
    project.loose.push_back(dependency);
    auto scoped = project.schemaScope("A");
    assert(scoped.loose.size() == 1);
    bool rejected = false;
    try {
        scoped.inventory();
    } catch (const std::exception &error) {
        rejected =
            std::string(error.what()).find("Missing schema header for B") != std::string::npos;
    }
    assert(rejected);
    // Even removal of every B file must retain the declared import in the
    // validation scope: a previous native B header can still exist in Working.
    project.loose.clear();
    const auto absent = project.schemaScope("A");
    assert(absent.scopeSchemas.contains("B"));
    const json managed = {{"B.scm", {{"schema", "B"}, {"parent", "RootSchema"}}}};
    const auto plan = InputInventory::compare(
        json::object(), InputInventory::withinSchemas(managed, absent.scopeSchemas), true);
    assert(plan.removedSchemas == std::vector<std::string>{"B"});
    std::cout << "Locally imported classes cannot bypass their missing schema header\n";
}
