#include "CommandLine.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(), project = root / "project",
         profilesFile = root / "machine" / "connections.json", install = root / "Jade";
    ConnectionProfiles(profilesFile).save("demo", {{"installPath", utf8(install.wstring())}});
    writeText(project / "Pet.cls", "");
    CommandLine cli(profilesFile);
    auto prepared = cli.prepare("build", {"Pet.cls"}, project);
    auto minimal = nlohmann::json::parse(readText(project / "jade.json"));
    assert(minimal.size() == 1 && minimal["connection"] == "demo");
    assert(prepared.config["databasePath"] == utf8((install / "system").wstring()));
    assert(prepared.config["workspaceRoots"][0] == utf8(project.wstring()));
    auto bytes = readText(project / "jade.json");
    auto again = cli.prepare("build", {"Pet.cls"}, project);
    assert(readText(project / "jade.json") == bytes);
    auto other = root / "second";
    auto initialized = cli.prepare(
        "init", {"--profile", "other", "--install", utf8((root / "OtherJade").wstring())}, other);
    assert(initialized.toJson()["initialization"]["created"] == true);
    assert(nlohmann::json::parse(readText(other / "jade.json"))["connection"] == "other");
    std::filesystem::remove_all(root);
    std::cout << "CommandLine init and automatic project binding passed\n";
}
