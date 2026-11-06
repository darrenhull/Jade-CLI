#include "InspectorSession.hpp"
#include "engine/ProjectModel.hpp"
#include "engine/PrivateFile.hpp"
#include <fstream>
#include <thread>
#include <iostream>
namespace jade::native {
InspectorSession::InspectorSession(const std::filesystem::path &executable,
                                   const std::filesystem::path &directory,
                                   const nlohmann::json &config,
                                   const std::string &selectedIdentity)
    : identity(selectedIdentity) {
    std::filesystem::create_directories(directory);
    configuration_ = directory / (uniqueId() + ".json");
    try {
        PrivateFile::write(configuration_, config.dump());
        process_.start(executable, {configuration_.wstring()}, directory);
        auto ready = nlohmann::json::parse(process_.line(nullptr, 30000, {}));
        if (ready.value("event", "") != "ready")
            throw std::runtime_error(
                ready.value("message", "Native inspection failed to connect."));
    } catch (...) {
        const auto initial = std::current_exception();
        close();
        std::rethrow_exception(initial);
    }
}
InspectorSession::~InspectorSession() {
    try {
        close();
    } catch (const std::exception &) {
        std::cerr << "Failed to remove a private inspection session configuration after closing "
                     "its native process.\n";
    }
}
nlohmann::json InspectorSession::request(const std::string &command, const nlohmann::json &args,
                                         std::atomic_bool *cancelled) {
    auto request = args;
    request["id"] = ++next_;
    request["command"] = command;
    if (request.contains("limit"))
        request["limit"] = std::clamp(request["limit"].get<int>(), 1, 100);
    process_.write(request.dump() + "\n");
    for (;;) {
        auto result = nlohmann::json::parse(process_.line(cancelled, 120000, {}));
        if (result.value("id", size_t(0)) != next_)
            continue;
        if (!result.value("success", false))
            throw std::runtime_error(result.value("message", "Native inspection request failed."));
        return result.value("body", nlohmann::json::object());
    }
}
void InspectorSession::close() {
    if (process_.alive()) {
        try {
            request("close", nlohmann::json::object(), nullptr);
            process_.waitExit(3000);
        } catch (...) {
            // A failed transport still needs owned-process termination before cleanup.
        }
    }
    process_.stop();
    PrivateFile::remove(configuration_);
}
} // namespace jade::native
