#include "ProjectModel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
static std::string cls(const std::string &schema, const std::string &name,
                       const std::string &parent, const std::string &extra = "") {
    return "schemaDefinition\n" + schema +
           " subschemaOf RootSchema partialDefinition;\ntypeHeaders\n" + name + " subclassOf " +
           parent + " transient;\ntypeDefinitions\n" + name +
           " completeDefinition\n(\n)\ntypeSources\n" + name +
           " (\njadeMethodSources\nworks\n{\nworks();\nvars\n" + extra + "\nbegin\nend;\n}\n)\n";
}
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto schemas = root / "schemas";
    writeText(schemas / "A.scm", "schemaDefinition\nA subschemaOf RootSchema partialDefinition;\n");
    writeText(schemas / "B.scm", "schemaDefinition\nB subschemaOf A partialDefinition;\n");
    writeText(root / "arbitrary" / "PetA.cls", cls("A", "Pet", "Object"));
    writeText(root / "other" / "PetB.cls", cls("B", "Pet", "Object"));
    writeText(root / "tests" / "Test.cls", cls("B", "PetTests", "JadeTestCase", "pet: Pet;"));
    writeText(root / "node_modules" / "Ignored.cls", cls("A", "Ignored", "Object"));
    auto project = ProjectModel::scan({root, root / "tests"});
    assert(project.schemas.size() == 2);
    assert(project.schemas[0].name == "A");
    assert(project.schemas[1].classes.size() == 2);
    auto scope = project.dependencies(root / "tests" / "Test.cls");
    assert(scope.schemas[0].classes.empty());
    assert(scope.schemas[1].classes.size() == 2);
    auto inventory = project.inventory();
    assert(inventory.size() == 5);
    writeText(root / "duplicate" / "Pet.cls", cls("B", "Pet", "Object"));
    bool duplicate = false;
    try {
        ProjectModel::scan({root});
    } catch (const std::exception &e) {
        duplicate = std::string(e.what()).find("Multiple complete class") != std::string::npos;
    }
    assert(duplicate);
    std::filesystem::remove(root / "duplicate" / "Pet.cls");
    writeText(schemas / "A.scm", "schemaDefinition\nA subschemaOf B partialDefinition;\n");
    bool cycle = false;
    try {
        ProjectModel::scan({root});
    } catch (const std::exception &e) {
        cycle = std::string(e.what()).find("Cyclic schema") != std::string::npos;
    }
    assert(cycle);
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel identities, dependency ancestry, overlapping roots, duplicate and "
                 "cycle edges passed\n";
}
