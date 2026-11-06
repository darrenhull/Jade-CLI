#include "WorkingEngine.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto root = std::filesystem::temp_directory_path() / jade::native::uniqueId();
    std::filesystem::create_directories(root / "source");
    jade::native::writeText(root / "source" / "_control.dat", "test");
    jade::native::writeText(root / "source" / "jade.ini", "[Jade]\nServer=MultiUser\n");
    jade::native::WorkingEngine engine(root);
    jade::native::json config = {
        {"program", jade::native::utf8((root / "workspace" / "Pet.cls").wstring())},
        {"databasePath", jade::native::utf8((root / "source").wstring())},
        {"installPath", jade::native::utf8(root.wstring())},
        {"storagePath", jade::native::utf8((root / "source").wstring())},
        {"workingDatabaseDirectory", jade::native::utf8((root / "source" / "nested").wstring())},
        {"workspaceRoots", {jade::native::utf8((root / "workspace").wstring())}}};
    auto r = engine.execute({{"command", "build"}, {"config", config}});
    assert(r["passed"] == false);
    assert(r["report"]["error"].get<std::string>().find("separate") != std::string::npos);
    assert(jade::native::readText(root / "source" / "_control.dat") == "test");
    std::filesystem::remove_all(root);
    std::cout << "WorkingEngine isolation contracts passed\n";
}
