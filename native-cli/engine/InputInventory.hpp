#pragma once
#include "ProjectModel.hpp"
namespace jade::native {
struct InputPlan {
    bool changed = false, structural = false;
    std::vector<std::string> deltaKeys, removedSchemas;
};
class InputInventory {
  public:
    static InputPlan compare(const json &current, const json &previous, bool completeProject);
};
} // namespace jade::native
