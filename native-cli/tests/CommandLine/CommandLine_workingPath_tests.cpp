#include "CommandLine.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(),
         configRoot = root / "configuration", invocation = root / "terminal";
    std::filesystem::create_directories(invocation);
    writeText(configRoot / "jade.json", R"({"workingDatabaseDirectory":"working"})");
    writeText(configRoot / "Pet.cls", "");
    auto prepared = CommandLine().prepare(
        "build", {"--config", "../configuration/jade.json", "../configuration/Pet.cls"},
        invocation);
    assert(prepared.config["workingDatabaseDirectory"] == utf8((configRoot / "working").wstring()));
    writeText(configRoot / "jade.json", R"({"workingDatabaseDirectory":""})");
    auto empty = CommandLine().prepare(
        "build", {"--config", "../configuration/jade.json", "../configuration/Pet.cls"},
        invocation);
    assert(!empty.config.contains("workingDatabaseDirectory"));
    std::filesystem::remove_all(root);
    std::cout << "CommandLine working directory resolves relative to config, empty remains unset\n";
}
