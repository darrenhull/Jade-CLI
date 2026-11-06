#include "DdxMerger.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto file = root / "bad.ddx";
    auto reject = [&](const std::string &text) {
        writeText(file, text);
        try {
            DdxMerger::merge("Demo", {file});
            return false;
        } catch (...) {
            return true;
        }
    };
    assert(reject("<schema name=\"Other\"/>"));
    assert(reject("<schema name=\"Demo\"><bad></schema>"));
    assert(
        reject("<!DOCTYPE schema [<!ENTITY external SYSTEM 'file:///C:/Windows/win.ini'>]><schema "
               "name=\"Demo\"><x>&external;</x></schema>"));
    auto a = root / "a.ddx", b = root / "b.ddx";
    writeText(a, "<schema name=\"Demo\" JadeVersionNumber=\"25.0.01\"><x name=\"one\"/></schema>");
    writeText(b, "<schema name=\"Demo\" JadeVersionNumber=\"25.0.02\"><x name=\"two\"/></schema>");
    bool mismatch = false;
    try {
        DdxMerger::merge("Demo", {a, b});
    } catch (...) {
        mismatch = true;
    }
    assert(mismatch);
    std::filesystem::remove_all(root);
    std::cout << "DdxMerger malformed, schema/version mismatch and XXE edges passed\n";
}
