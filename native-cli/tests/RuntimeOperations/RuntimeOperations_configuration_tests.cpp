#include "RuntimeOperations.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto profiles = root / "private" / "connections.json";
    _wputenv_s(L"JADE_TOOLS_CONNECTIONS_FILE", profiles.c_str());
    ConnectionProfiles(profiles).save("demo", {{"installPath", utf8((root / "Jade").wstring())}});
    RuntimeOperations runtime(root, root / "broker");
    auto selected = runtime.execute(
        {{"command", "inspect.target"},
         {"target", "source"},
         {"config", {{"connection", "demo"}, {"workspaceRoots", {utf8(root.wstring())}}}}});
    assert(selected["config"]["databasePath"] == utf8((root / "Jade" / "system").wstring()));
    assert(selected["config"]["iniPath"] ==
           utf8((root / "Jade" / "system" / "jade.ini").wstring()));
    auto initialized = runtime.execute({{"command", "cli"},
                                        {"operation", "init"},
                                        {"cwd", utf8((root / "project").wstring())},
                                        {"argv", nlohmann::json::array()}});
    assert(initialized["passed"] == true && initialized["connection"] == "demo");
    assert(std::filesystem::is_regular_file(root / "project" / "jade.json"));
    bool setup = false;
    try {
        runtime.execute({{"command", "working.location"}, {"config", nlohmann::json::object()}});
    } catch (const std::exception &e) {
        setup = std::string(e.what()).find("jade-cli init") != std::string::npos;
    }
    assert(setup);
    _wputenv_s(L"JADE_TOOLS_CONNECTIONS_FILE", L"");
    std::filesystem::remove_all(root);
    std::cout << "RuntimeOperations resolves minimal service profiles and noninteractive init\n";
}
