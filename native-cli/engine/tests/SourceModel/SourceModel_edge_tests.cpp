#include "SourceModel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <iostream>
int main() {
    std::string source =
        "schemaDefinition\nChildSchema subschemaOf ParentSchema partialDefinition;\n/* "
        "typeHeaders\nFake subclassOf Other; */\ntypeHeaders\nBaseUnitTest subclassOf JadeTestCase "
        "abstract, transient;\ntypeDefinitions\nBaseUnitTest "
        "completeDefinition\n(\n)\ntypeSources\nBaseUnitTest "
        "(\njadeMethodSources\nignored\n{\nignored() unitTest, unitTestIgnore;\nbegin\nwrite \"'' "
        "Fake subclassOf Foo;\";\nend;\n}\nplain\n{\nplain(a: String; b: Integer): "
        "String;\nbegin\n return a;\nend;\n}\n)\n";
    auto m = jade::native::SourceModel::parse(source, "BaseUnitTest.cls");
    assert(m.schema == "ChildSchema");
    assert(m.classes.size() == 1);
    assert(m.classes[0].parent == "JadeTestCase");
    assert(m.methods.size() == 2);
    assert(m.methods[0].unitTest && m.methods[0].ignored);
    assert(!m.methods[1].unitTest);
    assert(m.code.size() == source.size());
    assert(std::count(m.code.begin(), m.code.end(), '\n') ==
           std::count(source.begin(), source.end(), '\n'));
    std::cout << "SourceModel abstract, ignored tests, parameters and lexical edges passed\n";
}
