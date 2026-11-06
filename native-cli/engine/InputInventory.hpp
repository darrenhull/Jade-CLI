#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
struct InputPlan {
    bool changed = false, structural = false;
    std::vector<std::string> deltaKeys, removedSchemas;
};
class InputInventory {
  public:
    static void requireRemovalHeaders(const json &current, const json &previous,
                                      const std::set<std::string> &headerSchemas);
    static InputPlan compare(const json &current, const json &previous, bool completeProject);
    static json withinSchemas(const json &inventory, const std::set<std::string> &schemas);
    static json replaceSchemas(const json &previous, const json &current,
                               const std::set<std::string> &schemas);
};
} // namespace jade::native
