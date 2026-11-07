#include "CommandLine.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    const auto root = std::filesystem::temp_directory_path() / ("jade-workspace-cli-" + uniqueId());
    const auto configuration = root / "configuration", invocation = root / "terminal";
    std::filesystem::create_directories(invocation);
    writeText(configuration / "jade.json",
              R"({"installPath":"runtime","schema":"SavedSchema","program":"OldClass.cls"})");
    writeText(invocation / "scratch space.jade", "begin\nwrite 42;\nend;\n");
    CommandLine cli;
    auto prepared = cli.prepare("workspace.run",
                                {"--config", "../configuration/jade.json", "scratch space.jade",
                                 "--schema", "SelectedSchema", "--json"},
                                invocation);
    assert(prepared.files.size() == 1 && prepared.files[0] == invocation / "scratch space.jade");
    assert(prepared.config["schema"] == "SelectedSchema");
    assert(prepared.config["installPath"] == utf8((configuration / "runtime").wstring()));
    assert(!prepared.projectBuild);
    auto snapshot =
        cli.prepare("workspace.run", {"--config", "../configuration/jade.json"}, invocation);
    assert(snapshot.files.empty()); // exact JSONL snapshots must never run a stale configured CLS
    assert(snapshot.config["schema"] == "SavedSchema");
    bool rejected = false;
    try {
        cli.prepare(
            "workspace.run",
            {"--config", "../configuration/jade.json", "scratch space.jade", "scratch space.jade"},
            invocation);
    } catch (const std::exception &) {
        rejected = true;
    }
    assert(rejected);
    rejected = false;
    try {
        cli.prepare("workspace.run", {"--config", "../configuration/jade.json", "missing.jade"},
                    invocation);
    } catch (const std::exception &) {
        rejected = true;
    }
    assert(rejected);
    std::filesystem::remove_all(root);
    std::cout << "Workspace terminal paths, schema override, exact-snapshot selection and input "
                 "rejection passed\n";
}
