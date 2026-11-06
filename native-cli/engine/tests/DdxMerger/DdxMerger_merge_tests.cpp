#include "DdxMerger.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto root = std::filesystem::temp_directory_path() / jade::native::uniqueId();
    auto a = root / "a.ddx", b = root / "b.ddx";
    jade::native::writeText(
        a, "<schema name=\"Demo\" CompleteDefinition=\"false\"><application name=\"A\"/></schema>");
    jade::native::writeText(
        b, "<schema name=\"Demo\" CompleteDefinition=\"false\"><application name=\"B\"/></schema>");
    auto joined = jade::native::DdxMerger::merge("Demo", {a, b});
    assert(joined.find("name=\"A\"") != std::string::npos);
    assert(joined.find("name=\"B\"") != std::string::npos);
    assert(joined.find("CompleteDefinition=\"true\"") != std::string::npos);
    bool rejected = false;
    try {
        jade::native::DdxMerger::merge("Demo", {a, a});
    } catch (...) {
        rejected = true;
    }
    assert(rejected);
    std::filesystem::remove_all(root);
    std::cout << "DdxMerger contracts passed\n";
}
