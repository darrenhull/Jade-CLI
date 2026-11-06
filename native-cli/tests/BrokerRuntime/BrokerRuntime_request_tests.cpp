#include "BrokerTestFixture.hpp"
#include <cassert>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <windows.h>

using namespace jade::native;
#undef assert
#define assert(value)                                                                              \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            std::cerr << "Failed: " #value " at line " << __LINE__ << std::endl;                   \
            throw std::runtime_error("Broker assertion failed.");                                  \
        }                                                                                          \
    } while (false)
int wmain(int argc, wchar_t **argv) {
    BrokerOptions activeOptions;
    try {
        if (auto handled = BrokerTestFixture::handle(argc, argv))
            return *handled;
        auto options = BrokerTestFixture::options();
        auto root = std::filesystem::temp_directory_path() /
                    ("jade-native-broker-test-" + std::to_string(GetCurrentProcessId()));
        options.stateDirectory = root;
        activeOptions = options;
        std::vector<std::future<nlohmann::json>> concurrent;
        for (int i = 0; i < 8; ++i)
            concurrent.push_back(std::async(std::launch::async, [&] {
                return BrokerRuntime::request(options, {{"command", "status"}});
            }));
        nlohmann::json concurrentPid;
        for (auto &call : concurrent) {
            auto pid = call.get().at("brokerPID");
            if (concurrentPid.is_null())
                concurrentPid = pid;
            assert(pid == concurrentPid);
        }
        std::string output;
        auto first =
            BrokerRuntime::request(options, {{"command", "echo"}, {"value", "Unicode \u00a3"}},
                                   [&](auto text) { output += text; });
        assert(first.at("passed") == true && first.at("echo").at("value") == "Unicode \u00a3");
        assert(output == "native-output\n");
        auto inspectA = BrokerRuntime::request(
            options,
            {{"command", "cli"}, {"operation", "inspect.catalog"}, {"sessionId", "chosen"}});
        auto inspectB = BrokerRuntime::request(
            options,
            {{"command", "cli"}, {"operation", "inspect.catalog"}, {"sessionId", "chosen"}});
        assert(inspectA.at("echo").at("sessionId") != "chosen");
        assert(inspectA.at("echo").at("sessionId") != inspectB.at("echo").at("sessionId"));
        assert(inspectA.at("sessionId") == "chosen");
        auto status = BrokerRuntime::request(options, {{"command", "status"}});
        assert(status.at("brokerPID").get<unsigned>() != GetCurrentProcessId());
        const auto pid = status.at("brokerPID");
        assert(BrokerRuntime::request(options, {{"command", "status"}}).at("brokerPID") == pid);
        auto other = options;
        other.fingerprint = "wrong-artifact";
        bool refused = false;
        try {
            BrokerRuntime::request(other, {{"command", "echo"}});
        } catch (const std::exception &) {
            refused = true;
        }
        assert(refused);
        std::istringstream input("{\"id\":\"a\",\"command\":\"wait\"}\n{\"id\":\"b\",\"command\":"
                                 "\"cancel\",\"operationId\":\"a\"}\n");
        std::ostringstream packets;
        assert(BrokerRuntime::service(options, input, packets) == 0);
        bool cancelled = false;
        std::istringstream lines(packets.str());
        std::string line;
        while (std::getline(lines, line)) {
            auto packet = nlohmann::json::parse(line);
            if (packet.at("id") == "a" && packet.value("event", "") == "result")
                cancelled = packet.at("result").value("cancelled", false);
        }
        assert(cancelled);
        auto failure = BrokerRuntime::request(options, {{"command", "fail"}});
        assert(failure.at("passed") == false &&
               failure.at("report").at("error") == "deliberate native operation failure");
        assert(BrokerRuntime::request(options, {{"command", "echo"}}).at("passed") == true);
        auto waiting = std::async(std::launch::async, [&] {
            return BrokerRuntime::request(options, {{"command", "wait"}});
        });
        Sleep(100);
        assert(BrokerRuntime::request(options, {{"command", "status"}}).at("activeJobs").empty());
        assert(BrokerRuntime::request(options, {{"command", "cancel"}, {"operationId", "1"}})
                   .at("cancelled") == false);
        assert(waiting.get().at("passed") == true);
        const auto helperDir = options.executable.parent_path() / L"native";
        std::filesystem::create_directories(helperDir);
        std::filesystem::copy_file(options.executable, helperDir / L"jade-inspect-helper.exe",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto session = root / L"session.json";
        std::ofstream(session) << nlohmann::json{{"installPath", "not-a-database"},
                                                 {"databasePath", "fixture-only"}}
                                      .dump();
        auto helper = std::async(std::launch::async, [&] {
            std::istringstream commands("hold\nUnicode \xc2\xa3\n");
            std::ostringstream out, err;
            const int code =
                BrokerRuntime::relayHelper(options, "inspect", session, commands, out, err);
            return std::pair{code, out.str()};
        });
        bool busy = false;
        for (int i = 0; i < 100 && !busy; ++i) {
            Sleep(10);
            busy = BrokerRuntime::request(options, {{"command", "echo"}}).value("busy", false);
        }
        assert(busy);
        nlohmann::json helperOwners;
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            helperOwners =
                BrokerRuntime::request(options, {{"command", "status"}}).at("nativePIDs");
            if (!helperOwners.empty())
                break;
            Sleep(5);
        }
        assert(!helperOwners.empty());
        for (const auto &child : helperOwners)
            assert(child != pid);
        auto unowned =
            BrokerRuntime::request(options, {{"command", "helper.end"}, {"lease", "not-owned"}});
        assert(unowned.at("passed") == false);
        const auto helperResult = helper.get();
        std::cout << nlohmann::json{{"helperCode", helperResult.first},
                                    {"helperOutput", helperResult.second}}
                         .dump()
                  << std::endl;
        assert(helperResult.first == 0 && helperResult.second == "hold\r\nUnicode \xc2\xa3\r\n");
        auto after = BrokerRuntime::request(options, {{"command", "status"}});
        assert(after.at("activeJobs").empty());
        assert(after.at("nativePIDs").empty());
        assert(BrokerRuntime::request(options, {{"command", "shutdown"}}).at("passed") == true);
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid.get<DWORD>());
        if (process) {
            assert(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0);
            CloseHandle(process);
        }
        std::filesystem::remove_all(root);
        std::filesystem::remove(helperDir / L"jade-inspect-helper.exe");
        std::cout << "BrokerRuntime native process reuse, output, fingerprint, cancellation, "
                     "ownership and shutdown passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        if (!activeOptions.stateDirectory.empty())
            try {
                BrokerRuntime::request(activeOptions, {{"command", "shutdown"}});
            } catch (...) {
            }
        return 1;
    }
}
