#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    ConnectionProfiles profiles(root / "machine" / "connections.json");
    assert(profiles.names().empty());
    profiles.save("demo", {{"installPath", utf8((root / "Jade").wstring())}});
    assert(profiles.names() == std::vector<std::string>{"demo"});
    assert(profiles.get("demo")["installPath"] == utf8((root / "Jade").wstring()));
    bool conflicting = false;
    try {
        profiles.save("demo", {{"installPath", utf8((root / "Other").wstring())}});
    } catch (...) {
        conflicting = true;
    }
    assert(conflicting);
    bool missing = false;
    try {
        profiles.get("missing");
    } catch (...) {
        missing = true;
    }
    assert(missing);
    assert(std::filesystem::is_regular_file(root / "machine" / "connections.json"));
    std::filesystem::remove_all(root);
    std::cout << "ConnectionProfiles private save/load, conflict and unknown profile passed\n";
}
