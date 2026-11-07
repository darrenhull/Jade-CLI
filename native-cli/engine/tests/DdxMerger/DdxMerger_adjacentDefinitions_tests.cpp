#include "DdxMerger.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace jade::native;
    const auto root = std::filesystem::temp_directory_path() / ("jade-cli-ddx-" + uniqueId());
    auto require = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    try {
        const auto file = root / "package.ddx";
        for (const std::string separator : {"", "\r\n"}) {
            writeText(file,
                      "<schema name=\"Demo\" CompleteDefinition=\"false\">" +
                          separator +
                          "<Application name=\"DemoApp\"><defaultApp>true</defaultApp></Application>" +
                          separator +
                          "<JadeExportedPackage name=\"SharedTools\"><application "
                          "name=\"DemoApp\"/></JadeExportedPackage>" +
                          separator + "<Form name=\"LastForm\"/>" + separator + "</schema>");
            const auto output = DdxMerger::merge("Demo", {file});
            require(output.find("</Application>\r\n") != std::string::npos,
                    "Native DDX definitions must be emitted on separate Windows text lines");
            require(output.find("<Application name=\"DemoApp\"") != std::string::npos,
                    "Application definition was lost");
            require(output.find("<JadeExportedPackage name=\"SharedTools\"") != std::string::npos,
                    "Adjacent package definition was lost");
            require(output.find("<application name=\"DemoApp\"") != std::string::npos,
                    "Package application reference was lost");
            require(output.find("<Form name=\"LastForm\"") != std::string::npos,
                    "Last adjacent definition was lost");
        }
        writeText(file,
                  "<schema name=\"Demo\"><Application name=\"Same\"/>"
                  "<Application name=\"Same\"/></schema>");
        bool duplicateRejected = false;
        try {
            DdxMerger::merge("Demo", {file});
        } catch (...) {
            duplicateRejected = true;
        }
        require(duplicateRejected, "Adjacent duplicate identity was not rejected");
        std::filesystem::remove_all(root);
        std::cout << "DdxMerger preserves adjacent native definitions and checks every identity\n";
    } catch (const std::exception &error) {
        std::error_code cleanup;
        std::filesystem::remove_all(root, cleanup);
        std::cerr << error.what() << '\n';
        if (cleanup)
            std::cerr << "Owned test fixture retained: " << root << '\n';
        return 1;
    }
}
