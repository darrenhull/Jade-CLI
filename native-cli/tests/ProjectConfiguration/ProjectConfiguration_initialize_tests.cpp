#include "ProjectConfiguration.hpp"
#include "ConnectionProfiles.hpp"
#include "engine/ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(),
         file = root / "machine" / "connections.json";
    ConnectionProfiles profiles(file);
    profiles.save("one", {{"installPath", utf8((root / "One").wstring())}});
    auto single = ProjectConfiguration(file).initialize(root / "first" / "jade.json", root);
    assert(single["connection"] == "one");
    profiles.save("two", {{"installPath", utf8((root / "Two").wstring())}});
    bool ambiguous = false;
    try {
        ProjectConfiguration(file).initialize(root / "ambiguous" / "jade.json", root);
    } catch (const std::exception &e) {
        ambiguous = std::string(e.what()).find("--profile") != std::string::npos;
    }
    assert(ambiguous && !std::filesystem::exists(root / "ambiguous" / "jade.json"));
    int prompts = 0;
    auto chosen = ProjectConfiguration(file, [&](auto &question, auto &choices) {
                      ++prompts;
                      assert(choices.size() == 2);
                      return std::string("2");
                  }).initialize(root / "chosen" / "jade.json", root);
    assert(chosen["connection"] == "two" && prompts == 1);
    auto explicitChoice = ProjectConfiguration(file).initialize(root / "explicit" / "jade.json",
                                                                root, {{"profile", "one"}});
    assert(explicitChoice["connection"] == "one");
    auto legacy = root / "legacy" / "jade.json";
    std::string bytes =
        "{\"installPath\":\"C:/ExistingJade\",\"databasePath\":\"C:/ExistingBase\"}\n";
    writeText(legacy, bytes);
    auto unchanged = ProjectConfiguration(file).initialize(legacy, root, {{"profile", "two"}});
    assert(!unchanged["created"].get<bool>() && readText(legacy) == bytes);
    auto cloned = root / "cloned" / "jade.json";
    writeText(cloned, "{\"connection\":\"shared\"}\n");
    auto registered = ProjectConfiguration(file).initialize(
        cloned, root, {{"install", utf8((root / "SharedJade").wstring())}});
    assert(!registered["created"].get<bool>());
    assert(profiles.get("shared")["installPath"] == utf8((root / "SharedJade").wstring()));
    assert(nlohmann::json::parse(readText(cloned))["connection"] == "shared");
    std::filesystem::remove_all(root);
    std::cout << "ProjectConfiguration single/multiple/explicit/interactive selection and legacy "
                 "preservation passed\n";
}
