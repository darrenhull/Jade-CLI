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
    writeText(root / "Pet.ClS",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
              "subclassOf Object transient;\ntypeDefinitions\nPet "
              "completeDefinition\n(\n)\ntypeSources\n");
    writeText(root / "Broken.cls", "schemaDefinition\nDemo subschemaOf RootSchema "
                                   "partialDefinition;\ntypeHeaders\nBroken subclassOf\n");
    auto selected = ProjectModel::scan({root}).select({root / "Pet.ClS"});
    assert(selected.schemas[0].classes.size() == 1);
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel mixed-case CLS extension keeps explicit scope\n";
}
