#include "RuntimeOperations.hpp"
#include "Version.hpp"
#include <iostream>
#include <stdexcept>
using namespace jade::native;
int main() {
    try {
        RuntimeOperations runtime(std::filesystem::current_path(),
                                  std::filesystem::temp_directory_path() /
                                      "jade-native-dispatch-tests");
        auto doctor =
            runtime.execute({{"command", "doctor"}, {"config", nlohmann::json::object()}});
        if (doctor["runtimeImplementation"] != "native-cpp" || doctor["sdkVersion"] != cliVersion ||
            !doctor["passed"].get<bool>())
            throw std::runtime_error("doctor must truthfully identify native implementation");
        if (doctor["capabilities"].value("oidFormat", "") != "jade-native")
            throw std::runtime_error("doctor must advertise native Jade OID representation");
        bool rejected = false;
        try {
            runtime.execute({{"command", "native.run"},
                             {"config", {{"installPath", "C:/Jade2025"}}},
                             {"executable", "C:/Windows/System32/cmd.exe"},
                             {"args", nlohmann::json::array()}});
        } catch (...) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("native utility allowlist bypass");
        rejected = false;
        try {
            runtime.execute({{"command", "inspect.catalog"},
                             {"target", "other"},
                             {"config", nlohmann::json::object()}});
        } catch (...) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("inspection target not validated");
        rejected = false;
        try {
            runtime.execute({{"command", "arbitrary"}});
        } catch (...) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("unknown operations must fail");
        const auto targetConfig =
            nlohmann::json{{"installPath", "C:/missing-jade-runtime"},
                           {"databasePath", "C:/missing-jade-runtime/system"},
                           {"storagePath", std::filesystem::temp_directory_path().string()},
                           {"workspaceRoots", {std::filesystem::current_path().string()}}};
        const auto location =
            runtime.execute({{"command", "working.location"}, {"config", targetConfig}});
        if (!location.value("passed", false) || !location["working"].contains("databasePath") ||
            location["working"]["databasePath"] == targetConfig["databasePath"])
            throw std::runtime_error(
                "Working location must be resolved natively and separate from Source");
        const auto sourceTarget = runtime.execute(
            {{"command", "inspect.target"}, {"target", "source"}, {"config", targetConfig}});
        if (!sourceTarget["config"].value("readOnly", false) ||
            pathKey(std::filesystem::u8path(
                sourceTarget["config"]["databasePath"].get<std::string>())) !=
                pathKey(std::filesystem::u8path(targetConfig["databasePath"].get<std::string>())))
            throw std::runtime_error("Source inspection target identity changed");
        rejected = false;
        try {
            runtime.execute({{"command", "inspect.catalog"},
                             {"target", "source"},
                             {"config",
                              {{"installPath", "C:/missing-jade-runtime"},
                               {"databasePath", "C:/missing-jade-runtime/system"}}}});
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("missing inspector executable accepted");
        // Failed connection startup must not leave a null or half-owned session.
        runtime.disconnect();
        runtime.execute({{"command", "inspect.close"}});
        std::cout << "RuntimeOperations dispatch: PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
