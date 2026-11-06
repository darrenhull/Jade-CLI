#include "WorkingEngine.hpp"
#include <cassert>
#include <iostream>
int wmain() {
    using namespace jade::native;
    wchar_t exeName[32768];
    GetModuleFileNameW(nullptr, exeName, 32768);
    std::filesystem::path self(exeName);
    if (self.filename() == L"jdbutilb.exe" || self.filename() == L"jadloadb.exe" ||
        self.filename() == L"jadclient.exe") {
        Sleep(INFINITE);
        return 0;
    }
    if (self.filename() == L"jade-test-helper.exe") {
        std::cout << R"({"protocol":"jade-test/1","event":"ready","workingDatabase":true})"
                  << std::endl;
        std::string request;
        std::getline(std::cin, request);
        Sleep(INFINITE);
        return 0;
    }
    auto root = std::filesystem::temp_directory_path() / uniqueId(), install = root / "install",
         source = root / "seed", workspace = root / "workspace", distribution = root / "runtime";
    for (auto name : {L"jdbutilb.exe", L"jadloadb.exe", L"jadclient.exe"}) {
        std::filesystem::create_directories(install / L"bin");
        std::filesystem::copy_file(self, install / L"bin" / name);
    }
    std::filesystem::create_directories(distribution / L"native");
    std::filesystem::copy_file(self, distribution / L"native" / L"jade-test-helper.exe");
    writeText(source / "_control.dat", "fixture");
    writeText(source / "jade.ini", "[Jade]\nServer=SingleUser\n");
    writeText(workspace / "Pet.cls",
              "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
              "subclassOf Object transient;\ntypeDefinitions\nPet "
              "completeDefinition\n(\n)\ntypeSources\n");
    json config = {{"installPath", utf8(install.wstring())},
                   {"databasePath", utf8(source.wstring())},
                   {"storagePath", utf8((root / "private").wstring())},
                   {"program", utf8((workspace / "Pet.cls").wstring())},
                   {"workspaceRoots", {utf8(workspace.wstring())}},
                   {"timeoutMilliseconds", 100}};
    WorkingEngine engine(distribution);
    auto backup = engine.execute({{"command", "build"}, {"config", config}});
    assert(!backup.value("passed", true));
    assert(backup.value("timedOut", false));
    assert(!backup.value("cancelled", false));
    auto location = WorkingEngine::workingLocation(config);
    writeText(std::filesystem::path(wide(location["databasePath"])) / L"_control.dat", "fixture");
    writeText(std::filesystem::path(wide(location["root"])) / L"working-database.json",
              location.dump());
    auto helper = engine.execute({{"command", "build"}, {"config", config}});
    assert(!helper.value("passed", true));
    assert(helper.value("timedOut", false));
    assert(!helper.value("cancelled", false));
    engine.disconnect();
    std::filesystem::remove_all(root);
    std::cout << "WorkingEngine native utility and helper request timeouts retain timedOut flag\n";
}
