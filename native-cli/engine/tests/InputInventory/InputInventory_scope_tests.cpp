#include "InputInventory.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    const json previous = {
        {"a.scm",
         {{"kind", "resource"}, {"schema", "A"}, {"parent", "RootSchema"}, {"hash", "header"}}},
        {"removed.cls",
         {{"kind", "class"},
          {"schema", "A"},
          {"owner", "Removed"},
          {"shape", "x"},
          {"hash", "old"}}},
        {"b.scm",
         {{"kind", "resource"}, {"schema", "B"}, {"parent", "RootSchema"}, {"hash", "other"}}},
        {"keep.cls",
         {{"kind", "class"},
          {"schema", "B"},
          {"owner", "Keep"},
          {"shape", "y"},
          {"hash", "safe"}}}};
    const std::set<std::string> scope = {"A", "RootSchema"};
    const auto within = InputInventory::withinSchemas(previous, scope);
    assert(within.size() == 2 && !within.contains("b.scm"));
    const json current = {{"a.scm", previous["a.scm"]}};
    const auto plan = InputInventory::compare(current, within, true);
    assert(plan.changed && plan.structural && plan.removedSchemas.empty());
    const auto merged = InputInventory::replaceSchemas(previous, current, scope);
    assert(!merged.contains("removed.cls") && merged["keep.cls"] == previous["keep.cls"]);
    const auto invalidated = InputInventory::replaceSchemas(previous, json::object(), scope);
    assert(invalidated.size() == 2 && invalidated["b.scm"] == previous["b.scm"]);
    bool rejected = false;
    try {
        InputInventory::requireRemovalHeaders(current, within, {});
    } catch (const std::exception &error) {
        rejected = std::string(error.what()).find("schema header") != std::string::npos;
    }
    assert(rejected);
    InputInventory::requireRemovalHeaders(current, within, {"A"});
    auto moved = current;
    moved["moved.cls"] = previous["removed.cls"];
    InputInventory::requireRemovalHeaders(moved, {{"old.cls", previous["removed.cls"]}}, {});
    auto headerRemoved = moved;
    headerRemoved.erase("a.scm");
    bool missingHeader = false;
    try {
        InputInventory::requireRemovalHeaders(headerRemoved, within, {});
    } catch (const std::exception &e) {
        missingHeader = std::string(e.what()).find("managed schema header") != std::string::npos;
    }
    assert(missingHeader);
    std::cout << "Scoped complete inventories detect class deletion and preserve unrelated schemas "
                 "on success/failure\n";
}
