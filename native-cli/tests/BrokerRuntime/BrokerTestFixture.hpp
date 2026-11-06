#pragma once
#include "../../broker/BrokerRuntime.hpp"
#include <windows.h>
#include <iostream>
#include <optional>

class BrokerTestFixture {
  public:
    static jade::native::BrokerOptions options() {
        std::wstring executable(32768, L'\0');
        executable.resize(GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size())));
        jade::native::BrokerOptions options;
        options.executable = executable;
        options.version = "test";
        options.fingerprint = "test-artifact";
        return options;
    }
    static std::optional<int> handle(int argc, wchar_t **argv) {
        auto config = options();
        if (config.executable.filename() == L"jade-inspect-helper.exe") {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line == "exit42")
                    return 42;
                if (line == "split") {
                    std::cout << "\xf0" << std::flush;
                    Sleep(30);
                    std::cout << "\x9f\x90" << std::flush;
                    Sleep(30);
                    std::cout << "\x9f\n" << std::flush;
                    continue;
                }
                std::cout << line << std::endl;
                if (line == "hold")
                    Sleep(1000);
            }
            return 0;
        }
        for (int i = 2; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--state-dir")
                config.stateDirectory = argv[++i];
        if (argc > 1 && std::wstring(argv[1]) == L"client")
            return jade::native::BrokerRuntime::service(config, std::cin, std::cout);
        if (argc > 1 && std::wstring(argv[1]) == L"relay") {
            std::filesystem::path session;
            for (int i = 2; i + 1 < argc; ++i)
                if (std::wstring(argv[i]) == L"--session")
                    session = argv[++i];
            return jade::native::BrokerRuntime::relayHelper(config, "inspect", session, std::cin,
                                                            std::cout, std::cerr);
        }
        if (argc < 2 || std::wstring(argv[1]) != L"broker")
            return {};
        return jade::native::BrokerRuntime::runServer(
            config,
            [](const nlohmann::json &request, auto output, std::atomic_bool *cancelled) {
                const auto command = request.value("command", "");
                if (command == "crash") {
                    TerminateProcess(GetCurrentProcess(), 99);
                    throw std::runtime_error("unreachable");
                }
                if (command == "fail")
                    throw std::runtime_error("deliberate native operation failure");
                if (command == "flood") {
                    for (unsigned i = 0; i < 100 && !*cancelled; ++i)
                        output(std::string(65536, 'x'));
                    return nlohmann::json{{"code", 0}, {"passed", true}};
                }
                if (command == "wait") {
                    output("started\n");
                    for (unsigned i = 0; i < request.value("duration", 3000u) / 10 && !*cancelled;
                         ++i)
                        Sleep(10);
                    return nlohmann::json{{"code", *cancelled ? 130 : 0},
                                          {"passed", !*cancelled},
                                          {"cancelled", cancelled->load()}};
                }
                output("native-output\n");
                auto value = nlohmann::json{{"code", 0}, {"passed", true}, {"echo", request}};
                if (request.contains("sessionId"))
                    value["sessionId"] = request["sessionId"];
                return value;
            },
            [] {});
    }
};
