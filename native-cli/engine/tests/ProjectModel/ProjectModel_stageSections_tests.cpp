#include "ProjectModel.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

static std::string bytes(const std::filesystem::path &file) {
    std::ifstream stream(file, std::ios::binary);
    return {(std::istreambuf_iterator<char>(stream)), {}};
}

int main() {
    using namespace jade::native;
    const auto root = std::filesystem::temp_directory_path() / ("jade-cli-sections-" + uniqueId());
    auto require = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    try {
        const std::string source =
            "importedPackageDefinitions\n"
            "\tCatalog is Exporter::SharedTools\n"
            "\t(\n"
            "\timportedClassDefinitions\n"
            "\t\tThing // expos\xC3\xA9\n"
            "\t)\n";
        std::string expected;
        for (const char value : source)
            expected += value == '\n' ? "\r\n" : std::string(1, value);

        for (int encoding = 0; encoding != 4; ++encoding) {
            const auto workspace = root / std::to_string(encoding);
            const auto schema = workspace / "schemas" / "Consumer";
            const auto resource = schema / "packages" / "importedPackageDefinitions.sec";
            writeText(workspace / "schemas" / "Consumer.scm",
                      "jadeVersionNumber \"25.0.02\";\nschemaDefinition\n"
                      "Consumer subschemaOf RootSchema completeDefinition;\n");
            std::string original = encoding == 1 ? expected : source;
            if (encoding == 3)
                std::replace(original.begin(), original.end(), '\n', '\r');
            if (encoding == 2) {
                std::string encoded("\xFF\xFE", 2);
                for (const wchar_t value : wide(original)) {
                    encoded += static_cast<char>(value & 255);
                    encoded += static_cast<char>((value >> 8) & 255);
                }
                original = std::move(encoded);
            }
            writeText(resource, original);
            auto project = ProjectModel::scan({workspace});
            require(project.schemas.size() == 1, "Expected one schema");
            require(project.schemas[0].resources.size() == 1, "Expected one package resource");
            project.stage(workspace / "staged");
            const auto staged = workspace / "staged" / "s0" / resource.filename();
            require(readText(staged) == expected, "Native SEC newlines were not canonical");
            require(bytes(staged) == std::string("\xEF\xBB\xBF", 3) + expected,
                    "Native SEC encoding or content changed");
            require(bytes(resource) == original, "Original source bytes changed");
        }
        std::filesystem::remove_all(root);
        std::cout << "ProjectModel stages canonical private SEC newlines without rewriting source\n";
    } catch (const std::exception &error) {
        std::error_code cleanup;
        std::filesystem::remove_all(root, cleanup);
        std::cerr << error.what() << '\n';
        if (cleanup)
            std::cerr << "Owned test fixture retained: " << root << '\n';
        return 1;
    }
}
