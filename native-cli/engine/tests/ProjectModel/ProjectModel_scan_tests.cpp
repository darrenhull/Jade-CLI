#include "ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto root = std::filesystem::temp_directory_path() / jade::native::uniqueId();
    std::filesystem::create_directories(root / "schemas" / "Demo" / "types");
    jade::native::writeText(root / "schemas" / "Demo.scm",
                            "jadeVersionNumber \"25.0.02\";\nschemaDefinition\nDemo subschemaOf "
                            "RootSchema completeDefinition;\n");
    jade::native::writeText(
        root / "schemas" / "Demo" / "types" / "Pet.cls",
        "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
        "subclassOf Object transient;\ntypeDefinitions\nPet "
        "completeDefinition\n(\n)\ntypeSources\n");
    auto p = jade::native::ProjectModel::scan({root});
    assert(p.schemas.size() == 1);
    assert(p.schemas[0].classes.size() == 1);
    assert(p.schemas[0].classes[0].model.classes[0].name == "Pet");
    p.stage(root / "stage");
    assert(jade::native::readText(root / "stage" / "s0" / "schemaDefinitions.sec")
               .find("\tDemo subschemaOf") != std::string::npos);
    assert(std::filesystem::is_regular_file(root / "stage" / "s0" / "Types" / "Pet.cls"));
    assert(jade::native::readText(root / "stage" / "s0" / "typeHeaders.sec").find("Pet") !=
           std::string::npos);
    jade::native::writeText(root / "schemas" / "Z.scm",
                            "schemaDefinition\nZ subschemaOf RootSchema completeDefinition;\n");
    jade::native::writeText(root / "schemas" / "Demo" / "importedPackageDefinitions.sec",
                            "importedPackageDefinitions\nZPackage from Z;\n");
    auto packages = jade::native::ProjectModel::scan({root / "schemas"});
    assert(packages.schemas.front().name == "Z");
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel contracts passed\n";
}
