#include "CommandLine.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace jade::native;
namespace fs = std::filesystem;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Expected rejection");
}
int main() {
    try {
        const auto root = fs::temp_directory_path() / "jade-native-command-tests";
        fs::remove_all(root);
        fs::create_directories(root / "src" / "nested");
        std::ofstream(root / "jade.json")
            << R"({"installPath":"runtime","databasePath":"base","workspaceRoots":["src"]})";
        std::ofstream(root / "src" / "Fish.cls")
            << "schemaDefinition\nPets subschemaOf RootSchema partialDefinition;";
        CommandLine cli;
        auto project = cli.prepare("build", {}, root / "src" / "nested");
        require(project.projectBuild, "bare build must select project");
        require(project.config["databasePath"] == (root / "base").string(),
                "configuration paths relative to config");
        auto file = cli.prepare("build", {"../Fish.cls", "--json"}, root / "src" / "nested");
        require(file.files.size() == 1 && file.files[0] == root / "src" / "Fish.cls",
                "explicit file resolved relative to invocation");
        require(!file.projectBuild, "explicit build must not become project build");
        rejects([&] { cli.prepare("build", {"--unknown"}, root); });
        rejects([&] { cli.prepare("test", {"--config"}, root); });
        rejects([&] { cli.prepare("test", {"missing.cls"}, root); });
        rejects([&] { cli.prepare("build", {}, root.parent_path()); });
        auto positional = cli.prepare("build", {"--", "../Fish.cls"}, root / "src" / "nested");
        require(positional.files.size() == 1, "-- must terminate options");
        fs::remove_all(root);
        std::cout << "CommandLine prepare: PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
