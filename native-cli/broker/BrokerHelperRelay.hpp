#pragma once
#include "BrokerClient.hpp"
namespace jade::native::broker {
class BrokerHelperRelay {
  public:
    static int run(const BrokerOptions &options, const std::string &kind,
                   const std::filesystem::path &session, std::istream &input, std::ostream &output,
                   std::ostream &errors) {
        BrokerClient client(options);
        client.verify();
        client.send({{"id", "helper"},
                     {"command", "helper.open"},
                     {"kind", kind},
                     {"session", utf8(session)}});
        json packet;
        std::string lease;
        while (client.receive(packet)) {
            const auto event = packet.value("event", "");
            if (event == "helper.stdout") {
                output << packet.value("text", "");
                output.flush();
            } else if (event == "helper.stderr") {
                errors << packet.value("text", "");
                errors.flush();
            } else if (event == "result") {
                const auto result = packet.at("result");
                if (!result.value("passed", false))
                    throw std::runtime_error(result.value("report", json::object())
                                                 .value("error", "Native helper failed."));
                lease = result.at("lease");
                break;
            }
        }
        if (lease.empty())
            throw std::runtime_error("Native helper session closed before startup.");
        std::atomic_bool exited = false;
        int code = 2;
        std::thread reader([&] {
            try {
                json message;
                while (client.receive(message)) {
                    auto event = message.value("event", "");
                    if (event == "helper.stdout") {
                        output << message.value("text", "");
                        output.flush();
                    } else if (event == "helper.stderr") {
                        errors << message.value("text", "");
                        errors.flush();
                    } else if (event == "helper.exit") {
                        code = message.value("code", 2);
                        break;
                    }
                }
            } catch (const std::exception &error) {
                errors << error.what() << '\n';
            }
            exited = true;
            HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
            if (stdinHandle && stdinHandle != INVALID_HANDLE_VALUE)
                CancelIoEx(stdinHandle, nullptr);
        });
        std::string line;
        try {
            while (!exited && std::getline(input, line))
                client.send({{"id", "write"},
                             {"command", "helper.write"},
                             {"lease", lease},
                             {"text", line + "\n"}});
            if (!exited)
                client.send({{"id", "end"}, {"command", "helper.end"}, {"lease", lease}});
        } catch (...) {
            CancelIoEx(client.pipe, nullptr);
            CloseHandle(client.pipe);
            client.pipe = INVALID_HANDLE_VALUE;
            reader.join();
            throw;
        }
        reader.join();
        return code;
    }
};
} // namespace jade::native::broker
