#include "ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(), workspace = root / "workspace",
         outside = root / "external";
    writeText(workspace / "Demo.scm",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\n");
    auto make = [](const std::string &name, const std::string &extra = "") {
        return "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\n" +
               name + " subclassOf Object transient;\ntypeDefinitions\n" + name +
               " completeDefinition\n(\n" + extra + "\n)\ntypeSources\n";
    };
    writeText(workspace / "Test.cls", make("Test", "attributeDefinitions\npet: Pet;"));
    writeText(outside / "Pet.cls", make("Pet"));
    writeText(outside / "Unrelated.cls", make("Unrelated"));
    auto project = ProjectModel::scan({workspace}, {outside / "Pet.cls"});
    auto scope = project.dependencies(workspace / "Test.cls");
    assert(scope.schemas[0].classes.size() == 2);
    assert(project.schemas[0].classes.size() == 2);
    std::filesystem::remove_all(root);
    std::cout
        << "ProjectModel honors explicit external dependencies without scanning their siblings\n";
}
