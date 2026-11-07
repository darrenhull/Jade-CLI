#include "RuntimeOperations.hpp"
#include "../fixtures/DisposableWorkingDirectory.hpp"
#include "../fixtures/NativeOidFixture.hpp"
#include "Version.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    using namespace jade::native;
    try {
        const auto seedValue = std::getenv("JADE_CPP_OID_TEST_SEED");
        if (!seedValue) {
            std::cout << "SKIP: native OID inspection requires an isolated integration seed\n";
            return 0;
        }
        const auto seed = jade::tests::NativeOidFixture::seed(std::filesystem::u8path(seedValue));
        const auto staged = std::getenv("JADE_CPP_TEST_DISTRIBUTION");
        if (!staged || !*staged)
            throw std::runtime_error(
                "Set JADE_CPP_TEST_DISTRIBUTION to a verified staged native distribution.");
        const auto distribution = jade::tests::NativeOidFixture::distribution(
            std::filesystem::u8path(staged), cliVersion,
            [](const std::string &data) { return sha256(data); });
        DisposableWorkingDirectory fixture("jade-native-oid-inspection-");
        const auto root = fixture.path();
        std::filesystem::create_directories(root / L"src");
        RuntimeOperations runtime(distribution, root / L"sessions");
        const json config = {{"installPath", "C:/Jade2025"},
                             {"databasePath", utf8((seed / L"system").wstring())},
                             {"iniPath", utf8((seed / L"working.ini").wstring())},
                             {"workspaceRoots", {utf8((root / L"src").wstring())}},
                             {"storagePath", utf8((root / L"state").wstring())},
                             {"schema", "SJetSchema"},
                             {"server", "singleUser"}};
        runtime.execute({{"command", "working.prepare"}, {"config", config}});
        for (const auto *target : {"source", "working"}) {
            const auto request = [&](const std::string &operation, const json &args) {
                auto response = runtime.execute({{"command", "inspect." + operation},
                                                 {"target", target},
                                                 {"sessionId", target},
                                                 {"config", config},
                                                 {"args", args}});
                const auto body = response.at("body");
                if (body.value("oidFormat", "") != "jade-native")
                    throw std::runtime_error("Missing native OID response capability.");
                return body;
            };
            const auto tree = request("catalog", {{"includeSystem", true}});
            if (!tree.value("readOnly", false) || tree.at("schemas").empty())
                throw std::runtime_error("Native readonly catalog unavailable.");
            const auto oid = tree.at("schemas").at(0).at("oid").get<std::string>();
            if (oid.find('.') == std::string::npos)
                throw std::runtime_error("Catalog leaked a packed SDK identity.");
            const auto object = request("object", {{"oid", oid}});
            if (object.at("oid") != oid || !object.value("readOnly", false))
                throw std::runtime_error("Native canonical identity did not roundtrip.");
            // Native invalid IDs must remain operation failures, never empty success pages.
            bool rejected = false;
            try {
                request("object", {{"oid", "not-a-jade-oid"}});
            } catch (const std::exception &) {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Invalid native identity accepted.");
            request("close", json::object());
            std::cout << target << " readonly native OID catalog/object roundtrip PASS\n";
        }
        runtime.disconnect();
        fixture.remove();
        std::cout << "Native OID inspector sessions stopped; owned fixture removed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
