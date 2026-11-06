#include "NativeReports.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
static SourceEntry entry(const std::string &file, const std::string &text) {
    SourceEntry e;
    e.file = wide(file);
    e.text = text;
    e.model.schema = "Pets";
    return e;
}
static std::string record(const SourceEntry &file, int line, size_t position, size_t length = 6,
                          int code = 7041) {
    return "Compiler: Schema compile failed: file=" + utf8(file.file.wstring()) +
           ", schema=Pets, oid=[1.1], elapsed time=0, error=" + std::to_string(code) +
           ", pos=" + std::to_string(position) + ", len=" + std::to_string(length) +
           ", lineno=" + std::to_string(line) + "\n";
}
int main() {
    auto method = entry("C:/source/Cow.cls", "header\nreturn self.missing;\n");
    method.model.classes.push_back({"Cow", "Object", false});
    method.model.methods.push_back({"moo", "", 1, 2, false, false});
    const std::string legacy = "Compile error 6026 (Unknown property or method) in file "
                               "C:/private/method.scm in Pets::Cow::moo at line 1:\n";
    const auto original = entry("C:/source/Bad.cls", "header\nbreed: String[50 number = 1;\n");
    const auto loaded = entry("C:/private/joined.scm", "joined\nbreed: String[50 number = 1;\n");
    auto mixed =
        NativeReports::diagnostics(legacy + "Compile Error 7041 - Expecting: ]\n" +
                                       record(loaded, 2, wide(loaded.text).find(L"number")),
                                   {method, original, loaded});
    assert(mixed.size() == 2);
    assert(mixed[0]["compiler_error"] == 6026 && mixed[1]["compiler_error"] == 7041);
    const auto methodLoaded = entry("C:/private/method.scm", method.text);
    auto dedup = NativeReports::diagnostics(
        legacy + record(methodLoaded, 2, wide(methodLoaded.text).find(L"self"), 4, 6026),
        {method, methodLoaded});
    assert(dedup.size() == 1 && dedup[0]["method"] == "moo" && dedup[0]["length"] == 4);
    const auto other = entry("C:/source/Other.cls", "header\namount: Integer number = 2;\n");
    const auto otherLoaded = entry("C:/private/other.scm", "joined\namount: Integer number = 2;\n");
    auto repeated = NativeReports::diagnostics(
        "Compile Error 7041 - Expecting: ]\nCompile Error 7041 - Expecting: ;\n" +
            record(loaded, 2, wide(loaded.text).find(L"number")) +
            record(otherLoaded, 2, wide(otherLoaded.text).find(L"number")),
        {original, other, loaded, otherLoaded});
    assert(repeated.size() == 2 && repeated[0]["message"] == "Expecting: ]" &&
           repeated[1]["message"] == "Expecting: ;");
    const auto eof = entry("C:/private/eof.scm", "header\n");
    auto finalLine = NativeReports::diagnostics(
        "Compile Error 7041 - Expecting: )\n" + record(eof, 2, wide(eof.text).size(), 0), {eof});
    assert(finalLine.size() == 1 && finalLine[0]["line"] == 2 && finalLine[0]["column"] == 1 &&
           finalLine[0]["length"] == 0);
    const auto noNewline = entry("C:/private/no-newline.scm", "header");
    auto finalColumn =
        NativeReports::diagnostics("Compile Error 7041 - Expecting: )\n" +
                                       record(noNewline, 1, wide(noNewline.text).size(), 0),
                                   {noNewline});
    assert(finalColumn.size() == 1 && finalColumn[0]["column"] == 7);
    std::cout << "Mixed legacy/native batches, deduplication, repeated-code messages and EOF "
                 "positions passed\n";
}
