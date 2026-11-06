#include "NativeReports.hpp"
#include <cassert>
#include <iostream>

using namespace jade::native;
static SourceEntry source(const std::string &file, const std::string &text) {
    SourceEntry result;
    result.file = wide(file);
    result.text = text;
    result.model.schema = "Pets";
    return result;
}
static std::string compiler(const SourceEntry &loaded, const std::string &token, int line) {
    auto position = wide(loaded.text).find(wide(token));
    return "Compiler: Schema compile failed: file=" + utf8(loaded.file.wstring()) +
           ", schema=Pets, oid=[1000001.1], elapsed time=00:00:00.264, error=7041, pos=" +
           std::to_string(position) + ", len=6, lineno=" + std::to_string(line) + "\r\n";
}
int main() {
    const auto original =
        source("C:/source/Cow.cls",
               "header\r\nmetadata\r\n(\r\n        breed: String[50 number = 2;\r\n)\r\n");
    const auto joined = source("C:/private/joined.scm",
                               "schemaDefinition\r\n(\r\n  breed: String[50 number = 2;\r\n)\r\n");
    const auto output = "Compile Error 7041 - Expecting: ]\r\n" + compiler(joined, "number", 3);
    const auto report = NativeReports::diagnostics(output, {original, joined});
    assert(report.size() == 1);
    assert(report[0]["source"] == "C:/source/Cow.cls");
    assert(report[0]["line"] == 4);
    assert(report[0]["column"] == 26);
    assert(report[0]["length"] == 6);
    assert(report[0]["compiler_error"] == 7041);
    assert(report[0]["message"] == "Expecting: ]");

    auto duplicate = original;
    duplicate.file = L"C:/source/OtherCow.cls";
    const auto ambiguous = NativeReports::diagnostics(output, {original, duplicate, joined});
    assert(ambiguous.size() == 1);
    assert(ambiguous[0]["source"] == "C:/private/joined.scm");
    assert(ambiguous[0]["line"] == 3);
    assert(ambiguous[0]["generated_source"] == true);
    assert(NativeReports::diagnostics(output, {original}).empty()); // no untrusted log path reads

    const auto utf8Source =
        source("C:/source/Unicode.cls", "// caf\xc3\xa9\n  breed: String[50 number = 2;\n");
    const auto utf8Loaded =
        source("C:/private/unicode.scm", "// caf\xc3\xa9\n  breed: String[50 number = 2;\n");
    const auto unicode = NativeReports::diagnostics("Compile Error 7041 - Expecting: ]\n" +
                                                        compiler(utf8Loaded, "number", 2),
                                                    {utf8Source, utf8Loaded});
    assert(unicode.size() == 1 && unicode[0]["line"] == 2 && unicode[0]["column"] == 20);

    const auto existing = NativeReports::diagnostics(
        "Compile Error 7041 - Expecting: ]\r\nAt line 3:\r\n  breed: String[50 number = 2;\r\n",
        {original});
    assert(existing.size() == 1 && existing[0]["line"] == 4);
    auto method = source("C:/source/Cow.cls", "");
    method.model.classes.push_back({"Cow", "Object", false});
    method.model.methods.push_back({"moo", "", 10, 12, false, false});
    auto methodError = NativeReports::diagnostics(
        "Compile error 6026 (Unknown property or method) in file C:/private/joined.scm "
        "in Pets::Cow::moo at line 3:\r\n",
        {method});
    assert(methodError.size() == 1 && methodError[0]["line"] == 14);
    assert(methodError[0]["method"] == "moo" && methodError[0]["compiler_error"] == 6026);
    std::cout << "Native structural diagnostics map compiler positions to unique original lines; "
                 "ambiguous mappings retain native generated source\n";
}
