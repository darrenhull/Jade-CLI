#include "NativeTestSession.hpp"
#include "PrivateFile.hpp"
namespace jade::native {
NativeTestSession::~NativeTestSession() {
    try {
        disconnect();
    } catch (const std::exception &error) {
        OutputDebugStringA(error.what());
    }
}
void NativeTestSession::disconnect() {
    if (process_) {
        try {
            process_->write(json{{"id", ++sequence_}, {"command", "close"}}.dump() + "\n");
            process_->line(nullptr, 3000, {});
            process_->waitExit(3000);
        } catch (...) {
        }
        process_.reset();
    }
    key_.clear();
    if (!configFile_.empty()) {
        PrivateFile::remove(configFile_);
        configFile_.clear();
    }
}
json NativeTestSession::request(const std::filesystem::path &executable, const json &working,
                                const std::string &command, json args, NativeProcess::Output output,
                                std::atomic_bool *cancelled, unsigned timeout) {
    auto key =
        working.at("databasePath").get<std::string>() + working.at("iniPath").get<std::string>();
    if (process_ && (key_ != key || !process_->alive()))
        disconnect();
    try {
        if (!process_) {
            configFile_ = std::filesystem::path(wide(working.at("root"))) / L"test-sessions" /
                          (uniqueId() + ".json");
            json config = working;
            config["schema"] = "RootSchema";
            config["server"] = "singleUser";
            PrivateFile::write(configFile_, config.dump());
            process_ = std::make_unique<NativeProcess>();
            process_->start(executable, {configFile_.wstring()},
                            std::filesystem::path(wide(working.at("root"))));
            for (;;) {
                auto line = process_->line(cancelled, timeout, output);
                auto ready = json::parse(line, nullptr, false);
                if (ready.is_discarded() || ready.value("protocol", "") != "jade-test/1") {
                    if (output)
                        output(line + "\n");
                    continue;
                }
                if (ready.value("event", "") == "error")
                    throw std::runtime_error(
                        ready.value("message", "Native helper startup failed."));
                if (ready.value("event", "") == "ready") {
                    if (!ready.value("workingDatabase", false))
                        throw std::runtime_error(
                            "Native helper did not confirm Working isolation.");
                    break;
                }
            }
            key_ = key;
        }
        args["id"] = ++sequence_;
        args["command"] = command;
        process_->write(args.dump() + "\n");
        for (;;) {
            auto line = process_->line(cancelled, timeout, output);
            auto packet = json::parse(line, nullptr, false);
            if (packet.is_discarded() || packet.value("protocol", "") != "jade-test/1") {
                if (output)
                    output(line + "\n");
                continue;
            }
            if (packet.contains("id") && packet["id"] == sequence_) {
                if (!packet.value("success", false))
                    throw std::runtime_error(
                        packet.value("message", "Native Jade request failed."));
                auto body = packet.value("body", json::object());
                if (body.contains("output") && output)
                    output(body["output"]);
                auto ms =
                    body.value(command == "load" ? "loadMilliseconds" : "runMilliseconds", 0.0);
                if (output)
                    output("Native " + command + ": " + std::to_string(int(ms)) +
                           " ms; working connection retained.\n");
                return body;
            }
        }
    } catch (...) {
        auto original = std::current_exception();
        try {
            disconnect();
        } catch (const std::exception &cleanup) {
            if (output)
                output(std::string("Native session cleanup failed: ") + cleanup.what() + "\n");
        }
        std::rethrow_exception(original);
    }
}
} // namespace jade::native
