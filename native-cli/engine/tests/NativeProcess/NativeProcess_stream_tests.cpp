#include "NativeProcess.hpp"
#include "json.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace jade::native;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--ignore-input") {
            Sleep(30000);
            return 0;
        }
        if (argc > 1 && std::wstring(argv[1]) == L"--echo") {
            auto args = nlohmann::json::array();
            for (int i = 2; i < argc; ++i)
                args.push_back(utf8(argv[i]));
            std::cout << args.dump() << std::endl;
            std::cerr << "child-error" << std::flush;
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << line << std::endl;
            return 37;
        }
        std::wstring executable(32768, L'\0');
        executable.resize(GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size())));
        NativeProcess process;
        require(process.processId() == 0, "Unstarted process unexpectedly has a PID.");
        process.start(
            executable,
            {L"--echo", L"space here", L"trail\\", L"quote\"x", L"path=C:\\Jade2025\\system"}, {});
        require(process.processId() != 0 && process.processId() != GetCurrentProcessId(),
                "Native child process identity was not reported.");
        bool refused = false;
        try {
            process.exitCode();
        } catch (const std::exception &) {
            refused = true;
        }
        require(refused, "Running child was reported as completed.");
        process.write("Unicode \xf0\x9f\x90\x9f\n");
        process.endInput();
        process.endInput();
        refused = false;
        try {
            process.write("after EOF");
        } catch (const std::exception &) {
            refused = true;
        }
        require(refused, "Writing to closed native stdin was accepted.");
        std::string output, errors;
        const auto started = GetTickCount64();
        while (process.alive() && GetTickCount64() - started < 5000) {
            process.pump([&](auto text) { output += text; }, [&](auto text) { errors += text; });
            Sleep(5);
        }
        process.pump([&](auto text) { output += text; }, [&](auto text) { errors += text; });
        require(!process.alive(), "Native stdin EOF did not complete the child.");
        require(process.exitCode() == 37, "Native child exit code was not preserved.");
        const auto end = output.find('\n');
        const auto args = nlohmann::json::parse(output.substr(0, end));
        require(args == nlohmann::json::array(
                            {"space here", "trail\\", "quote\"x", "path=C:\\Jade2025\\system"}),
                "Windows argument quoting changed native argv.");
        require(output.substr(end + 1) == "Unicode \xf0\x9f\x90\x9f\r\n",
                "Native streaming Unicode changed.");
        require(errors == "child-error", "Native stderr was not delivered independently.");
        NativeProcess blocked;
        blocked.start(executable, {L"--ignore-input"}, {});
        auto writer = std::async(std::launch::async, [&] {
            try {
                blocked.write(std::string(2 * 1024 * 1024, 'x'));
            } catch (const std::exception &) {
            };
        });
        Sleep(50);
        auto stop = std::async(std::launch::async, [&] {
            blocked.stop();
            blocked.endInput();
        });
        const bool stopped = stop.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        if (!stopped) {
            HANDLE child = OpenProcess(PROCESS_TERMINATE, FALSE, blocked.processId());
            if (child) {
                TerminateProcess(child, 130);
                CloseHandle(child);
            }
        }
        stop.get();
        writer.get();
        require(stopped, "Stopping a child deadlocked behind its blocked stdin writer.");
        require(!blocked.alive(), "Stopped native process is still alive.");
        std::cout << "NativeProcess PID, stdin EOF, exit code, Unicode, quoting and blocked-writer "
                     "stop passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
