#include "NativeProcess.hpp"
#include <cassert>
#include <iostream>
int main() {
    using jade::native::NativeProcess;
    assert(NativeProcess::quote(L"") == L"\"\"");
    assert(NativeProcess::quote(L"a b") == L"\"a b\"");
    assert(NativeProcess::quote(L"a\\\"b") == L"\"a\\\\\\\"b\"");
    NativeProcess p;
    auto result =
        p.run(L"C:\\Windows\\System32\\where.exe", {L"cmd.exe"}, L".", nullptr, 10000, {});
    assert(result.code == 0);
    assert(result.output.find("cmd.exe") != std::string::npos);
    std::cout << "NativeProcess contracts passed\n";
}
