#include "NativeProcess.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--hang") {
        Sleep(INFINITE);
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"--error")
        return 47;
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    jade::native::NativeProcess timeout;
    auto start = std::chrono::steady_clock::now();
    auto result = timeout.run(executable, {L"--hang"}, L".", nullptr, 100, {});
    assert(result.timedOut && !result.success());
    assert(!timeout.alive());
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    std::atomic_bool cancelled = false;
    jade::native::NativeProcess cancel;
    std::thread trigger([&] {
        Sleep(70);
        cancelled = true;
    });
    auto aborted = cancel.run(executable, {L"--hang"}, L".", &cancelled, 5000, {});
    trigger.join();
    assert(aborted.cancelled && !aborted.success() && !cancel.alive());
    jade::native::NativeProcess failed;
    auto error = failed.run(executable, {L"--error"}, L".", nullptr, 5000, {});
    assert(error.code == 47 && !error.success());
    jade::native::NativeProcess missing;
    bool rejected = false;
    try {
        missing.run(L"C:\\no-such-native-tool-93843.exe", {}, L".", nullptr, 1000, {});
    } catch (...) {
        rejected = true;
    }
    assert(rejected);
    std::cout
        << "NativeProcess timeout, cancellation, nonzero and missing executable edges passed\n";
}
