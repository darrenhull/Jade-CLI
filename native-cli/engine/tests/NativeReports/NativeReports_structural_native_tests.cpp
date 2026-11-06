#include "WorkingEngine.hpp"
#include <cstdlib>
#include <iostream>
int main() {
    using namespace jade::native;
    auto seed = std::getenv("JADE_CPP_TEST_SEED");
    if (!seed) {
        std::cout << "SKIP: structural diagnostics proof requires a private TEMP seed\n";
        return 0;
    }
    auto source = std::filesystem::path(wide(seed));
    if (!contains(std::filesystem::temp_directory_path(), source) ||
        !source.parent_path().filename().wstring().starts_with(L"jade-cli-integration-seed-"))
        throw std::runtime_error("Use a disposable integration TEMP seed, never developer Source.");
    auto root = std::filesystem::temp_directory_path() / ("jade-structural-proof-" + uniqueId());
    auto src = root / L"src", file = src / L"StructuralProbe.cls";
    const std::string header = "jadeVersionNumber \"25.0.02\";\r\nschemaDefinition\r\n"
                               "SJetSchema subschemaOf RootSchema partialDefinition;\r\n";
    const auto text =
        header +
        "typeHeaders\r\nStructuralProbe subclassOf Object transient;\r\n"
        "typeDefinitions\r\nStructuralProbe completeDefinition\r\n(\r\nattributeDefinitions\r\n"
        "    breed: String[50 number = 1;\r\n)\r\ntypeSources\r\n";
    writeText(src / L"SJetSchema.scm", header);
    writeText(file, text);
    json config = {{"installPath", "C:/Jade2025"},
                   {"databasePath", utf8(source.wstring())},
                   {"iniPath", utf8((source.parent_path() / L"working.ini").wstring())},
                   {"storagePath", utf8((root / L"private").wstring())},
                   {"workspaceRoots", {utf8(src.wstring())}},
                   {"program", utf8(file.wstring())},
                   {"schema", "SJetSchema"},
                   {"server", "singleUser"}};
    WorkingEngine engine(std::filesystem::current_path());
    auto output = [](const auto &line) { std::cerr << line; };
    try {
        auto failed = engine.execute({{"command", "build"}, {"config", config}}, output);
        writeText(root / L"failure.json", failed.dump(2));
        const auto &diagnostics = failed.at("report").at("diagnostics");
        if (failed.value("passed", true) || failed.value("code", 0) != 7041 ||
            diagnostics.size() != 1 || diagnostics[0].value("source", "") != utf8(file.wstring()) ||
            diagnostics[0].value("line", 0) != 10 || diagnostics[0].value("length", 0) != 6)
            throw std::runtime_error("Native structural source mapping failed: " + failed.dump());
        auto fixed = text;
        fixed.replace(fixed.find("String[50 number"), 16, "String[50] number");
        writeText(file, fixed);
        auto recovered = engine.execute({{"command", "build"}, {"config", config}}, output);
        writeText(root / L"recovered.json", recovered.dump(2));
        if (!recovered.value("passed", false) || !recovered.at("report").at("diagnostics").empty())
            throw std::runtime_error("Native recovery retained stale diagnostics: " +
                                     recovered.dump());
        engine.disconnect();
        std::cout
            << "Native structural error 7041 mapped to original source, then cleared after fix: "
            << utf8(root.wstring()) << '\n';
    } catch (...) {
        engine.disconnect();
        throw;
    }
}
