#include "SourceCatalog.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace jade::native;
namespace fs = std::filesystem;
int main() {
    try {
        const auto root = fs::temp_directory_path() / "jade-native-catalog-tests";
        fs::remove_all(root);
        fs::create_directories(root / "odd" / "nested");
        fs::create_directories(root / "node_modules");
        const auto cls = R"(schemaDefinition
Pets subschemaOf RootSchema partialDefinition;
typeHeaders
FishTest subclassOf JadeTestCase transient;
typeDefinitions
FishTest completeDefinition
(
jadeMethodDefinitions
works() unitTest;
ignored() unitTest, unitTestIgnore;
)
typeSources
FishTest (
jadeMethodSources
works
{
works() unitTest;
begin
 write "Fake subclassOf Object; unitTest;";
end;
}
ignored
{
ignored() unitTest, unitTestIgnore;
begin
end;
}
)
)";
        std::ofstream(root / "odd" / "nested" / "DifferentFileName.cls") << cls;
        std::ofstream(root / "node_modules" / "Excluded.cls") << cls;
        SourceCatalog catalog;
        const auto files = catalog.files({root});
        if (files.size() != 1)
            throw std::runtime_error("discovery must ignore dependency folders");
        const auto tests =
            catalog.query("tests.list", {{"workspaceRoots", {root.string()}}}, "", 0, 100);
        if (tests["total"] != 1 || tests["rows"][0]["owner"] != "FishTest" ||
            tests["rows"][0]["method"] != "works")
            throw std::runtime_error(
                "unit test discovery must use source identities and ignore flagged tests");
        const auto classes =
            catalog.query("source.search", {{"workspaceRoots", {root.string()}}}, "fish", 0, 1);
        if (classes["total"] != 1 || classes["rows"][0]["schema"] != "Pets")
            throw std::runtime_error("schema-qualified class lookup");
        fs::remove_all(root);
        std::cout << "SourceCatalog discover: PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
