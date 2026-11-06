#include "SourceModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    const auto m = jade::native::SourceModel::parse(
        "schemaDefinition\nTests subschemaOf App partialDefinition;\ntypeHeaders\nPetsTests "
        "subclassOf JadeTestCase transient;\ntypeDefinitions\nPetsTests "
        "completeDefinition\n(\n)\ntypeSources\nPetsTests (\njadeMethodSources\nworks\n{\nworks() "
        "unitTest;\nbegin\n // no phantom: Broken subclassOf Thing;\n write \"fake() "
        "unitTest;\";\nend;\n}\n)",
        "PetsTests.cls");
    assert(m.schema == "Tests");
    assert(m.parentSchema == "App");
    assert(m.classes.size() == 1);
    assert(m.classes[0].name == "PetsTests");
    assert(m.methods.size() == 1);
    assert(m.methods[0].name == "works");
    assert(m.methods[0].unitTest);
    assert(m.methods[0].line == 12);
    std::cout << "SourceModel parser contract passed\n";
}
