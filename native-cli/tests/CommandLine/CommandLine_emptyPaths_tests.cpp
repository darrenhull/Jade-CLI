#include "CommandLine.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    writeText(
        root / "jade.json",
        R"({"installPath":"install","databasePath":"database","iniPath":"","storagePath":"","program":"","workspace":"","workingDatabaseDirectory":""})");
    writeText(root / "Pet.cls", "");
    auto prepared = CommandLine().prepare("build", {"Pet.cls"}, root);
    for (auto key : {"iniPath", "storagePath", "program", "workspace", "workingDatabaseDirectory"})
        assert(!prepared.config.contains(key));
    assert(prepared.files.size() == 1);
    writeText(root / "jade.json", R"({"iniPath":null})");
    bool badType = false;
    try {
        CommandLine().prepare("build", {"Pet.cls"}, root);
    } catch (...) {
        badType = true;
    }
    assert(badType);
    std::filesystem::remove_all(root);
    std::cout << "CommandLine empty optional paths preserve unset defaults\n";
}
