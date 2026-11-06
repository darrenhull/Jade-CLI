#include "NativeProcess.hpp"
#include "json.hpp"
#include <cassert>
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--unicode") {
        DWORD count;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\xE2", 1, &count, nullptr);
        Sleep(30);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\x82\xAC", 2, &count, nullptr);
        return 0;
    }
    if (argc > 2 && std::wstring(argv[1]) == L"--handle") {
        HANDLE handle = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
        SetEvent(handle);
        return 0;
    }
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    jade::native::NativeProcess unicode;
    bool valid = true;
    auto result =
        unicode.run(executable, {L"--unicode"}, L".", nullptr, 10000, [&](const std::string &text) {
            try {
                auto value = nlohmann::json(text).dump();
            } catch (...) {
                valid = false;
            }
        });
    assert(result.code == 0);
    assert(result.output == "\xE2\x82\xAC");
    assert(valid);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE secret = CreateEventW(&sa, TRUE, FALSE, nullptr);
    jade::native::NativeProcess isolated;
    auto child = isolated.run(executable,
                              {L"--handle", std::to_wstring(reinterpret_cast<uintptr_t>(secret))},
                              L".", nullptr, 10000, {});
    auto state = WaitForSingleObject(secret, 0);
    CloseHandle(secret);
    assert(child.code == 0);
    assert(state == WAIT_TIMEOUT);
    std::cout << "NativeProcess unicode and handle ownership passed\n";
}
