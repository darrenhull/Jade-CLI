#include "CommandLine.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace jade::native;
namespace fs = std::filesystem;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
int main() {
    try {
        const auto root = fs::temp_directory_path() / "jade-native-argument-edge-tests";
        fs::remove_all(root);
        fs::create_directories(root / "src");
        const auto config = root / "jade.json";
        auto write = [&](const std::string &text) {
            std::ofstream(config, std::ios::binary) << text;
        };
        std::ofstream(root / "src" / "Fish.cls") << "fixture";
        CommandLine cli;
        write(R"({"workspaceRoots":["src"]})");
        auto selected =
            cli.prepare("build", {"src/Fish.cls", "src/../src/Fish.cls", "SRC/FISH.CLS"}, root);
        require(selected.files.size() == 1,
                "Windows aliases must not duplicate one physical source input");
        auto afterSeparator = cli.prepare("build", {"--", "src/Fish.cls"}, root);
        require(afterSeparator.files.size() == 1, "positional terminator lost source");
        rejects([&] { cli.prepare("build", {"--program", "src/Fish.cls", "src/Fish.cls"}, root); },
                "mixed --program/source selection accepted");
        rejects([&] { cli.prepare("source.search", {"src/Fish.cls"}, root); },
                "source arguments accepted for discovery operation");
        rejects([&] { cli.prepare("build", {"src"}, root); }, "directory accepted as file");
        rejects([&] { cli.prepare("build", {"--workspace", "--json"}, root); },
                "option used as value");
        rejects([&] { cli.prepare("build", {std::string("src/Fish.cls\0hidden", 19)}, root); },
                "embedded NUL accepted in source path");
        write("{broken");
        rejects([&] { cli.prepare("build", {}, root); }, "malformed JSON accepted");
        write("[]");
        rejects([&] { cli.prepare("build", {}, root); }, "array config accepted");
        write(R"({"workspaceRoots":"src"})");
        rejects([&] { cli.prepare("build", {}, root); }, "string workspaceRoots accepted");
        write(R"({"workspaceRoots":[]})");
        rejects([&] { cli.prepare("build", {}, root); }, "empty workspaceRoots accepted");
        write(R"({"workspaceRoots":[7]})");
        rejects([&] { cli.prepare("build", {}, root); }, "nonstring workspace root accepted");
        write(std::string("\xef\xbb\xbf") + R"({"workspaceRoots":["src"]})");
        require(cli.prepare("build", {}, root).projectBuild, "UTF8 BOM configuration rejected");
        const auto unicode = fs::u8path("Pet-\xc3\xbc-\xce\xa9.cls");
        std::ofstream(root / "src" / unicode) << "fixture";
        auto named = cli.prepare("test", {"src/Pet-\xc3\xbc-\xce\xa9.cls"}, root);
        require(named.files.size() == 1 && named.files[0].filename() == unicode,
                "Unicode source path changed");
        fs::remove_all(root);
        std::cout << "CommandLine edge cases: PASS (13 contracts)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
