#include "BrokerTestFixture.hpp"
#include "BrokerTestChild.hpp"
#include "BrokerTestPipe.hpp"
#include <fstream>
#include <future>
#include <iostream>

using namespace jade::native;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    auto options = BrokerTestFixture::options();
    try {
        if (auto handled = BrokerTestFixture::handle(argc, argv))
            return *handled;
        options.stateDirectory = std::filesystem::temp_directory_path() /
                                 ("jade-broker-lifecycle-" + std::to_string(GetCurrentProcessId()));
        BrokerRuntime::request(options, {{"command", "status"}});
        std::ifstream tokenFile(options.stateDirectory / "token");
        std::string token;
        tokenFile >> token;
        tokenFile.close();
        {
            BrokerTestPipe owner(options.stateDirectory);
            const auto send = [&](const char *id, const char *command) {
                require(owner.write(nlohmann::json{{"id", id},
                                                   {"command", command},
                                                   {"sessionId", "objects"},
                                                   {"token", token},
                                                   {"fingerprint", options.fingerprint}}
                                        .dump() +
                                    "\n"),
                        "Cannot write inspection request.");
            };
            send("open", "inspect.catalog");
            // The fixture emits progress before its completion packet.
            while (nlohmann::json::parse(owner.line()).value("event", "") != "result") {
            }
            require(
                BrokerRuntime::request(options, {{"command", "inspection-count"}}).at("count") == 1,
                "Inspection fixture did not retain its native session.");
            std::promise<void> started;
            auto waiting = std::async(std::launch::async, [&] {
                return BrokerRuntime::request(options, {{"command", "wait"}, {"duration", 600}},
                                              [&](const std::string &) { started.set_value(); });
            });
            started.get_future().wait();
            send("close", "inspect.close");
            Sleep(50); // Queue the close behind the other client's running operation.
            owner.close();
            waiting.get();
        }
        bool released = false;
        for (unsigned attempt = 0; attempt < 100 && !released; ++attempt) {
            released =
                BrokerRuntime::request(options, {{"command", "inspection-count"}}).at("count") == 0;
            if (!released)
                Sleep(10);
        }
        require(released,
                "A disconnected client with a queued close leaked its inspection session.");
        {
            BrokerTestChild service(options.executable,
                                    {L"client", L"--state-dir", options.stateDirectory.wstring()});
            service.write("{\"id\":\"crash\",\"command\":\"crash\"}\n");
            require(service.waitForOutput(5000),
                    "Crashed broker did not report the pending request.");
            require(service.wait(2000),
                    "Service stayed alive after broker loss while stdin remained open.");
            require(service.code() == 2, "Lost broker service must report transport failure.");
        }
        {
            BrokerTestChild idle(options.executable,
                                 {L"client", L"--state-dir", options.stateDirectory.wstring()});
            idle.write("{\"id\":\"idle\",\"command\":\"status\"}\n");
            require(idle.waitForText("\"id\":\"idle\"", 5000),
                    "Idle service did not complete its initial request.");
            bool crashReported = false;
            try {
                BrokerRuntime::request(options, {{"command", "crash"}});
            } catch (const std::exception &) {
                crashReported = true;
            }
            require(crashReported, "The idle service's broker did not crash as requested.");
            require(idle.wait(2000), "Idle service stayed alive after unexpected broker loss.");
            require(idle.code() == 2, "Unexpected idle broker loss must report transport failure.");
        }
        DWORD lastBrokerPid = 0;
        {
            BrokerTestChild service(options.executable,
                                    {L"client", L"--state-dir", options.stateDirectory.wstring()});
            lastBrokerPid =
                BrokerRuntime::request(options, {{"command", "status"}}).at("brokerPID");
            service.write("{\"id\":\"stop\",\"command\":\"shutdown\"}\n");
            require(service.waitForText("\"id\":\"stop\"", 5000),
                    "Service shutdown did not return its result.");
            require(service.wait(2000), "Service shutdown left the process blocked on open stdin.");
            require(service.code() == 0, "Confirmed service shutdown must succeed.");
        }
        HANDLE lastBroker = OpenProcess(SYNCHRONIZE, FALSE, lastBrokerPid);
        if (lastBroker) {
            const auto exited = WaitForSingleObject(lastBroker, 5000);
            CloseHandle(lastBroker);
            require(exited == WAIT_OBJECT_0, "The final owned test broker did not exit.");
        }
        std::filesystem::remove_all(options.stateDirectory);
        std::cout << "BrokerRuntime queued close, pending/idle broker loss and graceful shutdown: "
                     "PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        try {
            BrokerRuntime::request(options, {{"command", "shutdown"}});
        } catch (...) {
        }
        return 1;
    }
}
