#include "../../cache_lifetime.hpp"
#include "json.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string>

int main() {
    using nlohmann::json;
    const std::map<std::string, bool> expected = {
        {"Final", false}, {"TransientAllowed", true}, {"PersistentAllowed", false},
        {"SharedTransientAllowed", true}, {"SubclassPersistentAllowed", true},
        {"SubclassSharedTransientAllowed", false}, {"SubclassTransientAllowed", false}};
    std::map<std::string, int> calls;
    json result = json::object();
    jade::capture_type_lifetime(result, [&](const char* symbol) {
        std::string entry(symbol);
        assert(entry.ends_with("@DskType@@UEBAHPEA_NI@Z"));
        const auto field = entry.substr(4, entry.find('@', 4) - 4);
        ++calls[field];
        return expected.at(field);
    });
    assert(result.size() == 7);
    assert(result["final"] == false);
    assert(result["transient_allowed"] == true);
    assert(result["persistent_allowed"] == false);
    assert(result["shared_transient_allowed"] == true);
    assert(result["subclass_persistent_allowed"] == true);
    assert(result["subclass_shared_transient_allowed"] == false);
    assert(result["subclass_transient_allowed"] == false);
    for (const auto& [field, value] : expected)
        assert(calls[field] == 1);
    jade::capture_type_lifetime(result, [](const char*) { return true; });
    for (const auto& value : result)
        assert(value == true);
    jade::capture_type_lifetime(result, [](const char*) { return false; });
    for (const auto& value : result)
        assert(value == false);
    json older = {{"transient_allowed", true}};
    assert(!older.contains("subclass_transient_allowed"));
    std::cout << "CacheLifetime captures native own/subclass flags without inventing absent fields\n";
}
