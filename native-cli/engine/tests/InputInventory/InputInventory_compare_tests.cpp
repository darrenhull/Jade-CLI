#include "InputInventory.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    json before = {
        {"schemaA",
         {{"kind", "resource"}, {"schema", "A"}, {"parent", "RootSchema"}, {"hash", "header"}}},
        {"schemaB", {{"kind", "resource"}, {"schema", "B"}, {"parent", "A"}, {"hash", "child"}}},
        {"a.cls",
         {{"kind", "class"},
          {"schema", "A"},
          {"owner", "AClass"},
          {"shape", "declarations"},
          {"hash", "old body"}}}};
    auto current = before;
    assert(!InputInventory::compare(current, before, true).changed);
    current["a.cls"]["hash"] = "new body";
    auto delta = InputInventory::compare(current, before, true);
    assert(delta.changed && !delta.structural && delta.deltaKeys.size() == 1);
    current["a.cls"]["shape"] = "new declaration";
    assert(InputInventory::compare(current, before, true).structural);
    json subset = {{"schemaA", before["schemaA"]}};
    auto partial = InputInventory::compare(subset, before, false);
    assert(!partial.changed);
    auto full = InputInventory::compare(subset, before, true);
    assert(full.changed && full.structural && full.removedSchemas.size() == 1 &&
           full.removedSchemas[0] == "B");
    auto removed = InputInventory::compare(json::object(), before, true);
    assert(removed.removedSchemas.size() == 2 && removed.removedSchemas[0] == "B" &&
           removed.removedSchemas[1] == "A");
    std::cout << "InputInventory method/structural/scope/deleted-schema plans passed\n";
}
