#include "NativeReports.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto m = SourceModel::parse(
        "schemaDefinition\nTests subschemaOf RootSchema partialDefinition;\ntypeHeaders\nDemoTests "
        "subclassOf JadeTestCase transient;\ntypeSources\nDemoTests "
        "(\njadeMethodSources\na\n{\na() unitTest;\nbegin\nend;\n}\nb\n{\nb() "
        "unitTest;\nbegin\nend;\n}\nignored\n{\nignored() unitTest, "
        "unitTestIgnore;\nbegin\nend;\n}\n)",
        "DemoTests.cls");
    auto ok = NativeReports::tests("total tests=2 failed=0 skipped=0 succeeded=2", m, {"a", "b"});
    assert(ok["tests_completed"] == true && ok["results"].size() == 2);
    auto ignored = NativeReports::tests("total tests=3 failed=0 skipped=1 succeeded=2", m, {});
    assert(ignored["tests_completed"].get<bool>());
    assert(ignored["results"][2]["status"] == "skipped");
    auto incomplete = NativeReports::tests("total tests=1 failed=0 skipped=0 succeeded=1", m, {});
    assert(!incomplete["tests_completed"].get<bool>());
    auto wrong = NativeReports::tests(
        " : Other::Someone::oops: bad\ntotal tests=2 failed=1 skipped=0 succeeded=1", m, {});
    assert(!wrong["tests_completed"].get<bool>());
    auto base = NativeReports::tests(
        " : RootSchema::JadeTestCase::assertEquals: expected X actual Y\nCall "
        "Stack:\nTests::DemoTests::b(50)\ntotal tests=2 failed=1 skipped=0 succeeded=1",
        m, {"a", "b"});
    assert(base["tests_completed"].get<bool>());
    assert(base["results"][1]["method"] == "b");
    assert(base["results"][1]["status"] == "failed");
    auto selected = NativeReports::tests("total tests=1 failed=0 skipped=0 succeeded=1", m, {"b"});
    assert(selected["tests_selected"] == 1);
    std::cout << "NativeReports success, ignored, incomplete, foreign and inherited failure edges "
                 "passed\n";
}
