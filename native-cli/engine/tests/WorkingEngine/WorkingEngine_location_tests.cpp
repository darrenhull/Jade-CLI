#include "WorkingEngine.hpp"
#include <iostream>
#include <stdexcept>
using namespace jade::native;
int main() {
    try {
        const json base = {{"installPath", "C:/test-jade"},
                           {"databasePath", "C:/test-source/system"},
                           {"storagePath", "C:/test-storage"},
                           {"workspaceRoots", {"C:/test-workspace"}}};
        const auto expected = WorkingEngine::workingLocation(base);
        auto empty = base;
        empty["workingDatabaseDirectory"] = "";
        empty["iniPath"] = "";
        empty["workingDatabaseGeneration"] = "";
        const auto actual = WorkingEngine::workingLocation(empty);
        if (actual != expected)
            throw std::runtime_error(
                "Empty optional VS Code settings must resolve exactly like omitted settings");
        auto changed = base;
        changed["workingDatabaseGeneration"] = "new-generation";
        if (WorkingEngine::workingLocation(changed)["identity"] == expected["identity"])
            throw std::runtime_error("Recreate generation must select a new Working database");
        changed = base;
        changed["schema"] = "AnotherSchema";
        if (WorkingEngine::workingLocation(changed)["identity"] != expected["identity"])
            throw std::runtime_error(
                "Class/schema switching must preserve one workspace Working database");
        std::cout << "WorkingEngine location defaults and generation: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
