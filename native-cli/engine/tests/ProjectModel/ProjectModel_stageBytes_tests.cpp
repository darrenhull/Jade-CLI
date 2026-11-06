#include "ProjectModel.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
static std::string raw(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    std::string text = "schemaDefinition\nDemo subschemaOf RootSchema "
                       "partialDefinition;\ntypeHeaders\nPet subclassOf Object "
                       "transient;\ntypeDefinitions\nPet completeDefinition\n(\n)\ntypeSources\n";
    for (bool utf16 : {false, true}) {
        auto source = root / (utf16 ? "Pet16.cls" : "Pet8.cls"),
             target = root / (utf16 ? "copy16.cls" : "copy8.cls");
        std::string bytes = utf16 ? std::string("\xFF\xFE", 2) : std::string("\xEF\xBB\xBF", 3);
        if (utf16) {
            for (wchar_t c : wide(text)) {
                bytes += char(c & 255);
                bytes += char((c >> 8) & 255);
            }
        } else
            bytes += text;
        writeText(source, bytes);
        SourceEntry e;
        e.file = source;
        e.text = readText(source);
        e.hash = sha256(bytes);
        stageSourceFile(e, target);
        assert(raw(target) == bytes);
        writeText(source, bytes + "changed");
        bool race = false;
        try {
            stageSourceFile(e, target);
        } catch (...) {
            race = true;
        }
        assert(race);
        std::filesystem::remove(source);
        std::filesystem::remove(target);
    }
    std::filesystem::remove_all(root);
    std::cout
        << "ProjectModel stages original UTF8 BOM/UTF16 bytes and rejects changed snapshots\n";
}
