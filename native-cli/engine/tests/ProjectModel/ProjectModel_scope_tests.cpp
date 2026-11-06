#include "ProjectModel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    writeText(root / "Demo.scm",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\n");
    writeText(root / "Pet.cls",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
              "subclassOf Object transient;\ntypeDefinitions\nPet "
              "completeDefinition\n(\n)\ntypeSources\n");
    writeText(root / "Broken.cls", "schemaDefinition\nDemo subschemaOf RootSchema "
                                   "partialDefinition;\ntypeHeaders\nBroken subclassOf\n");
    auto project = ProjectModel::scan({root});
    auto scope = project.dependencies(root / "Pet.cls");
    assert(scope.schemas.size() == 1);
    assert(scope.schemas[0].classes.size() == 1);
    assert(scope.schemas[0].classes[0].model.classes[0].name == "Pet");
    writeText(root / "Toy.cls",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nToy "
              "subclassOf Object transient;\ntypeDefinitions\nToy "
              "completeDefinition\n(\n)\ntypeSources\n");
    writeText(root / "Companion.cls",
              "schemaDefinition\nDemo subschemaOf RootSchema "
              "partialDefinition;\ntypeHeaders\nCompanion subclassOf RootSchemaApp "
              "transient;\ntypeDefinitions\nCompanion completeDefinition\n(\n)\ntypeSources\n");
    project = ProjectModel::scan({root});
    auto multi = project.select({root / "Pet.cls", root / "Toy.cls"});
    assert(multi.schemas[0].classes.size() == 3);
    bool outside = false;
    try {
        project.dependencies(root / "outside.cls");
    } catch (...) {
        outside = true;
    }
    assert(outside);
    auto loose = root / "bare";
    writeText(loose / "One.cls",
              "schemaDefinition\nBare subschemaOf RootSchema partialDefinition;\ntypeHeaders\nOne "
              "subclassOf Object transient;\ntypeDefinitions\nOne "
              "completeDefinition\n(\n)\ntypeSources\n");
    writeText(loose / "Two.cls",
              "schemaDefinition\nBare subschemaOf RootSchema partialDefinition;\ntypeHeaders\nTwo "
              "subclassOf Object transient;\ntypeDefinitions\nTwo "
              "completeDefinition\n(\n)\ntypeSources\n");
    auto withoutHeaders = ProjectModel::scan({loose}).select({loose / "One.cls"});
    assert(withoutHeaders.schemas.empty() && withoutHeaders.loose.size() == 1);
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel explicit class excludes unrelated malformed declaration\n";
}
