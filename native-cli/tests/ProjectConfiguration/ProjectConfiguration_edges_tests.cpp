#include "ProjectConfiguration.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
#include <functional>
int main() {
    using namespace jade::native;
    using json = nlohmann::json;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto file = root / "machine" / "connections.json";
    ProjectConfiguration config(file);
    auto rejects = [](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception &) {
            failed = true;
        }
        assert(failed);
    };
    auto priorInstall = _wgetenv(L"JADE_INSTALL");
    std::wstring install = priorInstall ? priorInstall : L"";
    auto priorHome = _wgetenv(L"JADE_HOME");
    std::wstring home = priorHome ? priorHome : L"";
    _wputenv_s(L"JADE_INSTALL", L"");
    _wputenv_s(L"JADE_HOME", L"");
    rejects([&] { config.initialize(root / "absent" / "jade.json", root); });
    assert(!std::filesystem::exists(root / "absent" / "jade.json"));
    rejects([&] { config.resolve({{"connection", "missing"}}, root, true); });
    rejects([&] { config.resolve({{"installPath", nullptr}}, root, true); });
    rejects([&] {
        config.resolve({{"installPath", "jade"}, {"workspaceRoots", json::array()}}, root, true);
    });
    rejects([&] { config.resolve({{"installPath", "jade"}, {"server", "invalid"}}, root, true); });
    rejects([&] { ConnectionProfiles(file).save("", {{"installPath", "jade"}}); });
    rejects([&] {
        ConnectionProfiles(file).save("bad", {{"installPath", "jade"}, {"databasePath", 42}});
    });
    auto unicode = root / L"Jade \u00e9 \u9c7c";
    auto setup = config.initialize(
        root / "first" / "jade.json", root,
        {{"install", utf8(unicode.wstring())}, {"profile", "demo"}, {"database", "custom"}});
    assert(setup["created"] == true);
    auto second = config.initialize(root / "second" / "jade.json", root);
    assert(second["connection"] == "demo");
    auto resolved = config.resolve({{"connection", "demo"}}, root / "second", true);
    assert(resolved["databasePath"] == utf8((root / "custom").wstring()));
    assert(resolved["iniPath"] == utf8((root / "custom" / "jade.ini").wstring()));
    auto other = config.resolve(
        {{"connection", "demo"}, {"databasePath", "override"}, {"iniPath", "config.ini"}},
        root / "second", true);
    assert(other["databasePath"] == utf8((root / "second" / "override").wstring()));
    assert(other["iniPath"] == utf8((root / "second" / "config.ini").wstring()));
    assert(!setup.contains("config") && !setup.contains("password"));
    writeText(file, "{broken");
    rejects([&] { ConnectionProfiles(file).names(); });
    _wputenv_s(L"JADE_INSTALL", install.c_str());
    _wputenv_s(L"JADE_HOME", home.c_str());
    std::filesystem::remove_all(root);
    std::cout << "ProjectConfiguration noninteractive failures, Unicode, reuse, overrides and "
                 "malformed profiles passed\n";
}
