#include "InputInventory.hpp"
#include <algorithm>
namespace jade::native {
void InputInventory::requireRemovalHeaders(const json &current, const json &previous,
                                           const std::set<std::string> &headerSchemas) {
    std::set<std::pair<std::string, std::string>> classes;
    for (const auto &[key, entry] : current.items())
        if (entry.value("kind", "") == "class")
            classes.emplace(entry.value("schema", ""), entry.value("owner", ""));
    for (const auto &[key, entry] : previous.items()) {
        const auto schema = entry.value("schema", ""), owner = entry.value("owner", "");
        if (entry.value("kind", "") == "resource" && entry.contains("parent") &&
            !headerSchemas.contains(schema))
            throw std::runtime_error("A managed schema header was removed: " + schema +
                                     ". Restore the schema header before executing scratch code; "
                                     "Working types were not updated.");
        if (entry.value("kind", "") == "class" && !classes.contains({schema, owner}) &&
            !headerSchemas.contains(schema))
            throw std::runtime_error("Cannot remove local class " + schema + "::" + owner +
                                     " without its schema header. Restore the header before "
                                     "executing scratch code; Working types were not updated.");
    }
}
json InputInventory::withinSchemas(const json &inventory, const std::set<std::string> &schemas) {
    auto result = json::object();
    for (const auto &[key, value] : inventory.items())
        if (schemas.contains(value.value("schema", "")))
            result[key] = value;
    return result;
}
json InputInventory::replaceSchemas(const json &previous, const json &current,
                                    const std::set<std::string> &schemas) {
    auto result = json::object();
    for (const auto &[key, value] : previous.items())
        if (!schemas.contains(value.value("schema", "")))
            result[key] = value;
    for (const auto &[key, value] : current.items())
        result[key] = value;
    return result;
}
InputPlan InputInventory::compare(const json &current, const json &previous, bool completeProject) {
    InputPlan plan;
    std::set<std::string> currentSchemas;
    for (auto &[key, value] : current.items()) {
        if (value.contains("schema"))
            currentSchemas.insert(value["schema"]);
        if (!previous.contains(key) || previous.at(key) != value) {
            plan.changed = true;
            plan.deltaKeys.push_back(key);
            if (!previous.contains(key) || value.value("kind", "") != "class" ||
                previous.at(key).value("shape", "") != value.value("shape", ""))
                plan.structural = true;
        }
    }
    std::map<std::string, std::string> oldParents;
    for (auto &[key, value] : previous.items()) {
        if (value.contains("schema") && value.contains("parent"))
            oldParents[value["schema"]] = value["parent"];
        if (!current.contains(key) &&
            (completeProject || (value.value("kind", "") == "resource" &&
                                 currentSchemas.contains(value.value("schema", ""))))) {
            plan.changed = true;
            plan.structural = true;
        }
    }
    if (completeProject) {
        for (auto &[schema, parent] : oldParents)
            if (!currentSchemas.contains(schema) && schema != "RootSchema" &&
                schema != "JadeSchema")
                plan.removedSchemas.push_back(schema);
        auto depth = [&](std::string name) {
            size_t result = 0;
            std::set<std::string> visited;
            while (oldParents.contains(name) && visited.insert(name).second) {
                ++result;
                name = oldParents.at(name);
            }
            return result;
        };
        std::sort(plan.removedSchemas.begin(), plan.removedSchemas.end(), [&](auto &a, auto &b) {
            auto da = depth(a), db = depth(b);
            return da == db ? a < b : da > db;
        });
    }
    return plan;
}
} // namespace jade::native
