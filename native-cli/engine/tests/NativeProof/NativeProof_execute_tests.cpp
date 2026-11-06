#include "WorkingEngine.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <regex>
int main() {
    auto seed = std::getenv("JADE_CPP_TEST_SEED");
    if (!seed) {
        std::cout << "SKIP: set JADE_CPP_TEST_SEED to a private disposable database\n";
        return 0;
    }
    auto source = std::filesystem::path(jade::native::wide(seed));
    if (!jade::native::contains(std::filesystem::temp_directory_path(), source))
        throw std::runtime_error("Proof seed must be a private TEMP database.");
    auto root =
        std::filesystem::temp_directory_path() / ("jade-native-proof-" + jade::native::uniqueId());
    auto src = root / "src";
    std::filesystem::create_directories(src);
    auto pet = src / "CppNativePet.cls", tests = src / "CppNativePetTests.cls";
    const std::string petText =
        "jadeVersionNumber \"25.0.02\";\nschemaDefinition\nSJetSchema subschemaOf RootSchema "
        "partialDefinition;\ntypeHeaders\nCppNativePet subclassOf Object "
        "transient;\ntypeDefinitions\nCppNativePet "
        "completeDefinition\n(\nattributeDefinitions\nname: "
        "String[50];\njadeMethodDefinitions\ngetName():String;\n)\ntypeSources\nCppNativePet "
        "(\njadeMethodSources\ngetName\n{\ngetName():String;\nbegin\n return "
        "self.name;\nend;\n}\n)\n";
    const std::string testText =
        "jadeVersionNumber \"25.0.02\";\nschemaDefinition\nSJetSchema subschemaOf RootSchema "
        "partialDefinition;\ntypeHeaders\nCppNativePetTests subclassOf JadeTestCase "
        "transient;\ntypeDefinitions\nCppNativePetTests "
        "completeDefinition\n(\njadeMethodDefinitions\nworks() "
        "unitTest;\n)\ntypeSources\nCppNativePetTests (\njadeMethodSources\nworks\n{\nworks() "
        "unitTest;\nvars\n pet: CppNativePet;\nbegin\n create pet transient;\n "
        "pet.name:=\"Native\";\n assertEquals(\"Native\",pet.getName());\nepilog\n delete "
        "pet;\nend;\n}\n)\n";
    jade::native::writeText(src / "SJetSchema.scm",
                            "jadeVersionNumber \"25.0.02\";\nschemaDefinition\nSJetSchema "
                            "subschemaOf RootSchema partialDefinition;\n");
    jade::native::writeText(pet, petText);
    jade::native::writeText(tests, testText);
    jade::native::json config = {
        {"installPath", "C:/Jade2025"},
        {"databasePath", jade::native::utf8(source.wstring())},
        {"iniPath", jade::native::utf8((source.parent_path() / "working.ini").wstring())},
        {"storagePath", jade::native::utf8((root / "private").wstring())},
        {"workspaceRoots", {jade::native::utf8(src.wstring())}},
        {"program", jade::native::utf8(tests.wstring())},
        {"server", "singleUser"}};
    auto distribution =
        std::filesystem::is_directory(std::filesystem::current_path() / "native-bin")
            ? std::filesystem::current_path()
            : std::filesystem::current_path() / "Jade-CLI";
    jade::native::WorkingEngine engine(distribution);
    auto output = [](auto &text) { std::cerr << text; };
    auto first = engine.execute({{"command", "test"}, {"config", config}}, output);
    std::cout << first.dump(2) << std::endl;
    if (!first.value("passed", false))
        return 1;
    auto second = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!second.value("passed", false) || !second["report"].value("native_source_reused", false))
        return 2;
    auto other = src / "CppNativePetOtherTests.cls";
    auto otherText = testText;
    for (size_t at = 0; (at = otherText.find("CppNativePetTests", at)) != std::string::npos;
         at += 22)
        otherText.replace(at, 17, "CppNativePetOtherTests");
    jade::native::writeText(other, otherText);
    config["program"] = jade::native::utf8(other.wstring());
    auto switched = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!switched.value("passed", false))
        return 3;
    config["program"] = jade::native::utf8(tests.wstring());
    auto returned = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!returned.value("passed", false) ||
        !returned["report"].value("native_source_reused", false))
        return 4;
    auto failingText = testText;
    auto expected = failingText.find("assertEquals(\"Native\"");
    failingText.replace(expected, 21, "assertEquals(\"Wrong\"");
    jade::native::writeText(tests, failingText);
    auto failure = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (failure.value("passed", true) || failure["report"].value("failures", 0) != 1 ||
        failure["report"]["results"].size() != 1 ||
        failure["report"]["results"][0].value("method", "") != "works")
        return 5;
    jade::native::writeText(tests, testText);
    auto invalid = petText;
    invalid.replace(invalid.find("self.name"), 9, "self.noSuchProperty");
    jade::native::writeText(pet, invalid);
    auto compileError = engine.execute({{"command", "build"}, {"config", config}}, output);
    std::cout << "Compiler error proof: " << compileError.dump(2) << std::endl;
    if (compileError.value("passed", true) || compileError.value("code", 0) == 0 ||
        compileError["report"]["diagnostics"].empty() ||
        compileError["report"]["diagnostics"][0].value("compiler_error", 0) != 6026)
        return 6;
    jade::native::writeText(pet, petText);
    auto recovered = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!recovered.value("passed", false))
        return 7;
    auto ddxInput =
        distribution / "native-cli" / "engine" / "tests" / "fixtures" / "SJetSchema.ddx";
    if (!std::filesystem::is_regular_file(ddxInput))
        throw std::runtime_error("Native DDX proof requires exported demo application source.");
    jade::native::writeText(src / "SJetSchema" / "applications" / "SJetSchema.ddx",
                            jade::native::readText(ddxInput));
    auto withDdx = engine.execute({{"command", "test"}, {"config", config}}, output);
    std::cout << "DDX proof: " << withDdx.dump(2) << std::endl;
    if (!withDdx.value("passed", false))
        return 8;
    auto renamePet = [](const std::string &text) {
        return std::regex_replace(text, std::regex(R"(\bCppNativePet\b)"), "CppNativeFish");
    };
    jade::native::writeText(pet, renamePet(petText));
    jade::native::writeText(tests, renamePet(testText));
    jade::native::writeText(other, renamePet(otherText));
    jade::native::writeText(src / "rename.jcf",
                            "JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nRename Class "
                            "SJetSchema::CppNativePet CppNativeFish\n");
    auto renamed = engine.execute({{"command", "test"}, {"config", config}}, output);
    std::cout << "Rename proof: " << renamed.dump(2) << std::endl;
    if (!renamed.value("passed", false))
        return 9;
    auto renameAgain = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!renameAgain.value("passed", false) ||
        !renameAgain["report"].value("native_source_reused", false))
        return 10;
    std::filesystem::remove(src / "rename.jcf");
    auto broken = src / "UnrelatedBroken.cls";
    auto brokenText = petText;
    for (size_t at = 0; (at = brokenText.find("CppNativePet", at)) != std::string::npos; at += 15)
        brokenText.replace(at, 12, "UnrelatedBroken");
    brokenText.replace(brokenText.find("self.name"), 9, "self.unknownProperty");
    jade::native::writeText(broken, brokenText);
    config["selectedFiles"] = jade::native::json::array({jade::native::utf8(pet.wstring())});
    config["program"] = jade::native::utf8(pet.wstring());
    auto explicitBuild = engine.execute({{"command", "build"}, {"config", config}}, output);
    if (!explicitBuild.value("passed", false))
        return 11;
    config["projectBuild"] = true;
    auto allBuild = engine.execute({{"command", "build"}, {"config", config}}, output);
    if (allBuild.value("passed", true) || allBuild["report"]["diagnostics"].empty())
        return 12;
    config["projectBuild"] = false;
    config["selectedFiles"] = jade::native::json::array(
        {jade::native::utf8(pet.wstring()), jade::native::utf8(tests.wstring())});
    config["program"] = jade::native::utf8(tests.wstring());
    auto multiBuild = engine.execute({{"command", "build"}, {"config", config}}, output);
    if (!multiBuild.value("passed", false))
        return 13;
    auto afterMulti = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!afterMulti.value("passed", false))
        return 14;
    config["testDependencyScope"] = "workspace";
    auto workspaceTest = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (workspaceTest.value("passed", true) || workspaceTest["report"]["diagnostics"].empty())
        return 15;
    config["testDependencyScope"] = "dependencies";
    auto scopedRecovery = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!scopedRecovery.value("passed", false))
        return 16;
    auto encodedPet = jade::native::readText(pet);
    encodedPet.replace(encodedPet.find("return self.name;"), 17, "return self.name & \"\";");
    auto utf16Bytes = [](const std::string &text) {
        std::string bytes("\xFF\xFE", 2);
        for (auto value : jade::native::wide(text)) {
            bytes += char(value & 255);
            bytes += char((value >> 8) & 255);
        }
        return bytes;
    };
    jade::native::writeText(pet, utf16Bytes(encodedPet));
    auto encodedBuild = engine.execute({{"command", "build"}, {"config", config}}, output);
    if (!encodedBuild.value("passed", false) || !encodedBuild["report"].value("incremental", false))
        return 17;
    auto location = jade::native::WorkingEngine::workingLocation(config);
    bool preserved = false;
    for (auto &entry : std::filesystem::recursive_directory_iterator(
             std::filesystem::path(jade::native::wide(location["root"])) / L"loads"))
        if (entry.is_regular_file() && entry.path().extension() == L".cls") {
            std::ifstream file(entry.path(), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(file)), {});
            if (bytes == utf16Bytes(encodedPet))
                preserved = true;
        }
    if (!preserved)
        return 18;
    auto external = root / "external" / "CppExternalHelper.cls";
    jade::native::writeText(external,
                            "jadeVersionNumber \"25.0.02\";\nschemaDefinition\nSJetSchema "
                            "subschemaOf RootSchema partialDefinition;\ntypeHeaders\n"
                            "CppExternalHelper subclassOf Object transient;\ntypeDefinitions\n"
                            "CppExternalHelper completeDefinition\n(\n)\ntypeSources\n");
    encodedPet.insert(encodedPet.find("name: String[50];"), "buddy: CppExternalHelper;\n");
    jade::native::writeText(pet, utf16Bytes(encodedPet));
    config["files"] = jade::native::json::array({jade::native::utf8(external.wstring())});
    auto externalBuild = engine.execute({{"command", "build"}, {"config", config}}, output);
    if (!externalBuild.value("passed", false))
        return 19;
    auto externalTest = engine.execute({{"command", "test"}, {"config", config}}, output);
    if (!externalTest.value("passed", false))
        return 20;
    engine.disconnect();
    jade::native::writeText(root / "proof.json",
                            jade::native::json{{"config", config},
                                               {"first", first},
                                               {"second", second},
                                               {"switch", switched},
                                               {"return", returned},
                                               {"failure", failure},
                                               {"compileError", compileError},
                                               {"recovered", recovered},
                                               {"ddx", withDdx},
                                               {"renamed", renamed},
                                               {"renameAgain", renameAgain},
                                               {"explicitBuild", explicitBuild},
                                               {"allBuild", allBuild},
                                               {"multiBuild", multiBuild},
                                               {"afterMulti", afterMulti},
                                               {"workspaceTest", workspaceTest},
                                               {"scopedRecovery", scopedRecovery},
                                               {"encodedBuild", encodedBuild},
                                               {"externalBuild", externalBuild},
                                               {"externalTest", externalTest}}
                                .dump(2));
    std::cout << "Native C++ proof: " << jade::native::utf8(root.wstring()) << std::endl;
    return 0;
}
