#include "NativeProcess.hpp"
#include <cassert>
#include <iostream>
int main(int argc, char **) {
    if (argc > 1) {
        if (std::cin.get() != std::char_traits<char>::eof())
            return 1;
        std::cout << "STDIN_EOF\n";
        return 0;
    }
    std::wstring executable(32768, 0);
    executable.resize(GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size())));
    jade::native::NativeProcess process;
    const auto result = process.run(executable, {L"read"}, L".", nullptr, 3000, {}, true);
    assert(result.success() &&
           (result.output == "STDIN_EOF\n" || result.output == "STDIN_EOF\r\n"));
    std::cout << "NativeProcess closes explicitly noninteractive child stdin without a timeout\n";
}
