#include "ProjectConfiguration.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(), project = root / "project",
         install = root / "Jade";
    ProjectConfiguration config(root / "machine" / "connections.json");
    auto minimal = config.resolve({{"installPath", utf8(install.wstring())}}, project, true);
    assert(minimal["databasePath"] == utf8((install / "system").wstring()));
    assert(minimal["iniPath"] == utf8((install / "system" / "jade.ini").wstring()));
    assert(minimal["workspaceRoots"][0] == utf8(project.wstring()));
    assert(minimal["server"] == "singleUser");
    assert(minimal.contains("storagePath"));
    ConnectionProfiles(root / "machine" / "connections.json")
        .save("demo", {{"installPath", utf8(install.wstring())}});
    auto reference =
        config.resolve({{"connection", "demo"}, {"databasePath", "other"}}, project, true);
    assert(reference["databasePath"] == utf8((project / "other").wstring()));
    assert(reference["iniPath"] == utf8((project / "other" / "jade.ini").wstring()));
    auto full = config.resolve({{"installPath", utf8(install.wstring())},
                                {"databasePath", utf8((root / "custom").wstring())},
                                {"iniPath", utf8((root / "ini").wstring())},
                                {"workspaceRoots", {utf8(project.wstring())}},
                                {"storagePath", utf8((root / "private").wstring())}},
                               project, true);
    assert(full["storagePath"] == utf8((root / "private").wstring()));
    std::filesystem::remove_all(root);
    std::cout << "ProjectConfiguration inferred defaults, connection reference, overrides and "
                 "legacy paths passed\n";
}
