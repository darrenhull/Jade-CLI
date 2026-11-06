#include "NativeReports.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto model = jade::native::SourceModel::parse(
        "schemaDefinition\nTests subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPetTest "
        "subclassOf JadeTestCase transient;\ntypeSources\nPetTest "
        "(\njadeMethodSources\nworks\n{\nworks() unitTest;\nbegin\nend;\n}\n)",
        "PetTest.cls");
    auto r = jade::native::NativeReports::tests(
        "2026 : Tests::PetTest::works: expected 1 but got 2\nCall "
        "Stack:\nTests::PetTest::works(3)\ntotal tests=1 failed=1 skipped=0 succeeded=0\n",
        model, {});
    assert(r["tests_completed"] == true);
    assert(r["failures"] == 1);
    assert(r["results"][0]["method"] == "works");
    assert(r["results"][0]["status"] == "failed");
    auto missing = jade::native::NativeReports::tests("", model, {});
    assert(missing["tests_completed"] == false);
    std::cout << "NativeReports contracts passed\n";
}
