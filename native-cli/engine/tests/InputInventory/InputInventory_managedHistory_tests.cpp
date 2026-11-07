#include "InputInventory.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    const json successful = {
        {"A.scm",
         {{"kind", "resource"}, {"schema", "A"}, {"parent", "RootSchema"}, {"hash", "good"}}},
        {"Old.cls", {{"kind", "class"}, {"schema", "A"}, {"owner", "Old"}}},
        {"B.scm",
         {{"kind", "resource"}, {"schema", "B"}, {"parent", "RootSchema"}, {"hash", "other"}}}};
    const json attempted = {
        {"A.scm",
         {{"kind", "resource"}, {"schema", "A"}, {"parent", "RootSchema"}, {"hash", "changed"}}},
        {"New.cls", {{"kind", "class"}, {"schema", "A"}, {"owner", "New"}}}};
    const auto managed = InputInventory::remember(successful, attempted);
    const auto claims = InputInventory::replaceSchemas(successful, json::object(), {"A"});
    assert(InputInventory::withinSchemas(claims, {"A"}).empty());
    assert(managed.contains("Old.cls") && managed.contains("New.cls") && managed.contains("B.scm"));
    bool rejected = false;
    try {
        InputInventory::requireRemovalHeaders(json::object(),
                                              InputInventory::withinSchemas(managed, {"A"}), {});
    } catch (const std::exception &e) {
        rejected = std::string(e.what()).find("managed schema header") != std::string::npos;
    }
    assert(rejected);
    auto replaced = successful;
    replaced["Old.cls"]["owner"] = "Renamed";
    replaced["A.scm"]["schema"] = "Retargeted";
    const auto renamedHistory = InputInventory::remember(successful, replaced);
    bool oldClass = false, oldHeader = false;
    for (const auto &[key, value] : renamedHistory.items()) {
        oldClass |= value.value("owner", "") == "Old";
        oldHeader |= value.value("schema", "") == "A" && value.contains("parent");
    }
    assert(oldClass && oldHeader);
    const auto committed = InputInventory::replaceSchemas(managed, attempted, {"A"});
    assert(!committed.contains("Old.cls") && committed.contains("B.scm"));
    std::cout << "Managed identity history survives failed-claim invalidation; successful scopes "
                 "replace it\n";
}
