#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CommandLine.hpp"
#include "RuntimeOperations.hpp"
#include "broker/BrokerRuntime.hpp"
#include "Version.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <io.h>
using namespace jade::native;
namespace fs = std::filesystem;
namespace {
fs::path executable() {
    std::wstring path(32768, 0);
    const auto size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!size || size == path.size())
        throw std::runtime_error("Cannot resolve CLI executable.");
    path.resize(size);
    return path;
}
std::string help() {
    return std::string("jade-cli ") + cliVersion +
           " (native C++)\nCommands: doctor, build, test, tests list, source search, "
           "inspect catalog|instances|object|collection, init, status, disconnect, "
           "shutdown\nUsage: "
           "jade-cli build [file.cls ...]; jade-cli test <file.cls> [more.cls ...]\nAutomatically "
           "discovers jade.json in this folder or its parents; missing build/test projects "
           "initialize automatically.\nSetup: jade-cli init --install C:\\Jade2025 [--profile "
           "local] [--database <folder>]\nOptions: --config <json> --profile <name> "
           "--workspace <folder> --query <text> --json\nIntegration: service --stdio; helper "
           "inspect|debug|compiler [session.json]\nBuild/test use an independent Working database. "
           "No publish command is exposed.\n";
}
ProjectConfiguration::Prompt terminalPrompt() {
    if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stderr)))
        return {};
    return [](const std::string &question, const std::vector<std::string> &choices) {
        std::cerr << question << "\n";
        for (size_t i = 0; i < choices.size(); ++i)
            std::cerr << "  " << i + 1 << ") " << choices[i] << "\n";
        std::cerr << "> " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer))
            throw std::runtime_error("Jade setup input ended. Run init with --install or --profile "
                                     "for noninteractive setup.");
        return answer;
    };
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    bool jsonOutput = false;
    for (int i = 1; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--json")
            jsonOutput = true;
    try {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i)
            args.push_back(utf8(argv[i]));
        if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
            std::cout << help();
            return 0;
        }
        const auto command = args[0];
        args.erase(args.begin());
        if (command == "init") {
            auto prepared = CommandLine({}, terminalPrompt()).prepare(command, args);
            auto result = prepared.initialization;
            result["passed"] = true;
            result["code"] = 0;
            std::cout << result.dump(jsonOutput ? -1 : 2) << '\n';
            return 0;
        }
        BrokerOptions options;
        options.executable = executable();
        options.nativeDirectory = options.executable.parent_path() / "native";
        options.version = cliVersion;
        if (const auto *state = _wgetenv(L"JADE_TOOLS_STATE_DIRECTORY"))
            options.stateDirectory = state;
        else if (const auto *local = _wgetenv(L"LOCALAPPDATA"))
            options.stateDirectory = fs::path(local) / L"JadeTools" / L"broker-v2-native";
        else
            throw std::runtime_error("LOCALAPPDATA is required for private native broker state.");
        if (command == "broker") {
            if (args.size() == 2 && args[0] == "--state-dir")
                options.stateDirectory = fs::u8path(args[1]);
            else if (!args.empty())
                throw std::runtime_error("Invalid internal broker arguments.");
            RuntimeOperations operations(options.executable.parent_path(), options.stateDirectory);
            return BrokerRuntime::runServer(
                options,
                [&](const auto &request, auto output, auto cancelled) {
                    return operations.execute(request, output, cancelled);
                },
                [&] { operations.disconnect(); });
        }
        if (command == "service") {
            if (args != std::vector<std::string>{"--stdio"})
                throw std::runtime_error("Usage: jade-cli service --stdio");
            return BrokerRuntime::service(options, std::cin, std::cout);
        }
        if (command == "helper") {
            if (args.empty() || args.size() > 2)
                throw std::runtime_error(
                    "Usage: jade-cli helper inspect|debug|compiler [session.json]");
            return BrokerRuntime::relayHelper(options, args[0],
                                              args.size() == 2 ? fs::u8path(args[1]) : fs::path(),
                                              std::cin, std::cout, std::cerr);
        }
        std::string operation = command;
        if (command == "tests" || command == "source" || command == "inspect") {
            if (args.empty())
                throw std::runtime_error("Subcommand required.");
            operation += '.' + args[0];
            args.erase(args.begin());
        }
        const auto prepared = CommandLine({}, terminalPrompt()).prepare(operation, args);
        nlohmann::json request;
        if (operation == "status" || operation == "disconnect" || operation == "shutdown")
            request = {{"command", operation}};
        else
            request = {{"command", "cli"},
                       {"operation", operation},
                       {"argv", args},
                       {"cwd", utf8(fs::current_path().wstring())}};
        const auto result = BrokerRuntime::request(
            options, request, [](const std::string &text) { std::cerr << text << std::flush; });
        std::cout << result.dump(jsonOutput ? -1 : 2) << '\n';
        return result.value("passed", true) ? 0 : result.value("cancelled", false) ? 130 : 1;
    } catch (const std::exception &e) {
        if (jsonOutput)
            std::cout << nlohmann::json(
                             {{"code", 2}, {"passed", false}, {"report", {{"error", e.what()}}}})
                             .dump()
                      << '\n';
        else
            std::cerr << e.what() << '\n';
        return 2;
    }
}
