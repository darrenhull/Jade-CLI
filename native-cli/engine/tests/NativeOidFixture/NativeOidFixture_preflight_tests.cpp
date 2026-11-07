#include "../fixtures/NativeOidFixture.hpp"
#include "../fixtures/DisposableWorkingDirectory.hpp"
#include <iostream>
using namespace jade::native;
using jade::tests::NativeOidFixture;
int wmain(int argc, wchar_t **argv) {
    try {
        DisposableWorkingDirectory fixture("jade-cli-integration-seed-preflight-");
        const auto root = fixture.path();
        writeText(root / "system/_control.dat", "not a native database");
        const auto ini = [&](const std::string &journal, const std::string &transient) {
            return "[PersistentDb]\r\nJournalRootDirectory=" + journal +
                   "\r\n[TransientDb]\r\nFileDirectory=" + transient + "\r\n";
        };
        writeText(root / "working.ini",
                  ini(utf8((root.parent_path() / "external-journals").wstring()),
                      utf8((root / "transient").wstring())));
        // Direct entries must reject unsafe INI paths without their PS wrappers.
        if (argc > 1) {
            const auto saved = [](const wchar_t *key) {
                const auto value = _wgetenv(key);
                return std::wstring(value ? value : L"");
            };
            const auto oldSeed = saved(L"JADE_CPP_OID_TEST_SEED"),
                       oldSdkSeed = saved(L"JADE_OID_TEST_SEED"), oldHome = saved(L"JADE_HOME"),
                       oldDistribution = saved(L"JADE_CPP_TEST_DISTRIBUTION");
            std::exception_ptr failure;
            try {
                _wputenv_s(L"JADE_CPP_OID_TEST_SEED", root.c_str());
                _wputenv_s(L"JADE_OID_TEST_SEED", root.c_str());
                _wputenv_s(L"JADE_HOME", L"");
                _wputenv_s(L"JADE_CPP_TEST_DISTRIBUTION", L"");
                for (int i = 1; i < argc; ++i) {
                    NativeProcess child;
                    const auto result = child.run(argv[i], {}, root, nullptr, 10000, {}, true);
                    if (result.code != 1 ||
                        result.output.find("mutable INI directory escapes") == std::string::npos)
                        throw std::runtime_error("Direct C++ entry bypassed fixture validation: " +
                                                 result.output);
                }
            } catch (...) {
                failure = std::current_exception();
            }
            _wputenv_s(L"JADE_CPP_OID_TEST_SEED", oldSeed.c_str());
            _wputenv_s(L"JADE_OID_TEST_SEED", oldSdkSeed.c_str());
            _wputenv_s(L"JADE_HOME", oldHome.c_str());
            _wputenv_s(L"JADE_CPP_TEST_DISTRIBUTION", oldDistribution.c_str());
            if (failure)
                std::rethrow_exception(failure);
            if (readText(root / "system/_control.dat") != "not a native database")
                throw std::runtime_error("Preflight changed the sentinel control file.");
            std::cout << "Both direct C++ entries rejected shared INI paths before native work\n";
        }
        bool rejected = false;
        try {
            NativeOidFixture::seed(root);
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error(
                "Copied INI escaped the C++ fixture preflight before native work.");
        rejected = false;
        try {
            NativeOidFixture::distribution(root, "0.4.1",
                                           [](const auto &bytes) { return sha256(bytes); });
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error(
                "Checkout/helper layout accepted without a verified staged manifest.");
        const auto ownJournal = utf8((root / "journals").wstring()),
                   ownTransient = utf8((root / "transient").wstring());
        const auto valid = ini(ownJournal, ownTransient);
        for (const auto &text :
             std::vector<std::string>{valid, std::regex_replace(valid, std::regex("\r\n"), "\n")}) {
            writeText(root / "working.ini", text);
            NativeOidFixture::seed(root);
        }
        for (const auto &text : std::vector<std::string>{
                 ini("<default>", ownTransient), ini("relative", ownTransient),
                 ini(ownJournal, "<default>"),
                 ini(ownJournal, utf8((root.parent_path() / "foreign-transient").wstring())),
                 valid + "[JadeEnvironment]\r\nProgramDataDirectory=" +
                     utf8((root.parent_path() / "foreign-data").wstring()) + "\r\n",
                 valid + "[PersistentDb]\r\nJournalRootDirectory=<default>\r\n"}) {
            writeText(root / "working.ini", text);
            rejected = false;
            try {
                NativeOidFixture::seed(root);
            } catch (const std::exception &) {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Unsafe C++ fixture path/default/duplicate accepted.");
        }
        const auto stage = root / "stage";
        json files = json::object();
        for (const auto *helper : {"jade-inspect-helper.exe", "jade-test-helper.exe",
                                   "jade-debug-helper.exe", "jade-native-helper.exe"}) {
            const auto name = std::string("native/") + helper;
            writeText(stage / name, name);
            files[name] = sha256(name);
        }
        writeText(stage / "jade-cli.exe", "fixture executable");
        const json manifest = {
            {"formatVersion", 2},           {"runtime", "native-cpp"},
            {"platform", "win32"},          {"arch", "x64"},
            {"packageVersion", "0.4.1"},    {"oidFormat", "jade-native"},
            {"executable", "jade-cli.exe"}, {"executableSha256", sha256("fixture executable")},
            {"runtimeFiles", files}};
        writeText(stage / "distribution.json", manifest.dump());
        const auto hash = [](const auto &data) { return sha256(data); };
        NativeOidFixture::distribution(stage, "0.4.1", hash);
        for (const auto *version : {"0.3.1", "0.4.0"}) {
            rejected = false;
            try {
                NativeOidFixture::distribution(stage, version, hash);
            } catch (const std::exception &) {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Old staged helper version accepted.");
        }
        writeText(stage / "native/jade-inspect-helper.exe", "tampered helper");
        rejected = false;
        try {
            NativeOidFixture::distribution(stage, "0.4.1", hash);
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Tampered staged inspector accepted.");
        std::cout << "NativeOidFixture native-open-free negative preflight passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
