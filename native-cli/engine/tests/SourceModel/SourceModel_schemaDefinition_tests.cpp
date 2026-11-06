#include "SourceModel.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    auto complete = SourceModel::parse(
        "schemaDefinition\nDemo subschemaOf RootSchema completeDefinition;\n", "");
    assert(complete.completeSchema);
    auto partial =
        SourceModel::parse("schemaDefinition\nDemo subschemaOf RootSchema "
                           "partialDefinition;\ntypeDefinitions\nPet completeDefinition\n(\n)\n",
                           "");
    assert(!partial.completeSchema);
    auto moved = SourceModel::parse(
        "// completeDefinition\nschemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\n",
        "");
    assert(!moved.completeSchema);
    std::cout << "Schema completeness comes from native declaration metadata, not class "
                 "definitions/comments\n";
}
