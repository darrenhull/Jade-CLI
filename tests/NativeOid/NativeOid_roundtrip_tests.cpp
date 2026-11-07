// Opt-in integration test: compile with the installed Jade SDK and run only
// against an explicitly supplied, disposable native integration seed.
#define wmain inspectorEntry
#include "../../native-src/inspector/inspector.cpp"
#undef wmain
#include <cassert>
#include "../../native-cli/engine/tests/fixtures/NativeOidFixture.hpp"

int main() {
    try {
        const auto variable = _wgetenv(L"JADE_OID_TEST_SEED");
        if (!variable)
            throw std::runtime_error("Set JADE_OID_TEST_SEED to an isolated integration seed.");
        const auto seed = jade::tests::NativeOidFixture::seed(variable);
        const auto jadeHome = _wgetenv(L"JADE_HOME");
        if (!jadeHome)
            throw std::runtime_error("Set JADE_HOME to the licensed Jade installation.");
        Runtime runtime({{"installPath", utf8(jadeHome)},
                         {"databasePath", utf8((seed / L"system").wstring())},
                         {"iniPath", utf8((seed / L"working.ini").wstring())},
                         {"server", "singleUser"},
                         {"readOnly", false}});
        assert(runtime.get(runtime.instances[6], "getSignOnUsage", DSKINTEGER).value.body.integer ==
               1);
        Inspector inspector(runtime);
        Oid sample{};
        sample.set(9002, 1);
        auto value = Param::oid(sample);
        const auto displayed = inspector.display(value);
        if (displayed.at("oid") != "9002.1" || displayed.at("value") != "9002.1")
            throw std::runtime_error(
                "Object-valued fields must expose Jade's native OID, not a memory dump.");
        auto plain = Param::string("01000000000000002a23000000000000000000000000");
        assert(inspector.display(plain).at("value") == utf8(*plain.text));
        std::vector<Oid> cases{sample};
        Oid variant{};
        variant.set(51, 1, 0, 0, 0, 0, LIFETIME_TRANSIENT);
        cases.push_back(variant);
        variant.set(51, 2, 0, 0, 0, 0, LIFETIME_SHAREDTRANSIENT);
        cases.push_back(variant);
        variant.set(66, 101, 305, 2, 1);
        cases.push_back(variant);
        variant.set(66, 101, 305);
        cases.push_back(variant);
        for (const auto &oid : cases) {
            const auto native = inspector.oids.format(oid);
            assert(inspector.oids.parse(native) == oid);
            assert(inspector.oids.parse(hex(&oid, sizeof oid)) == oid);
            std::cout << native << " roundtrip PASS\n";
        }
        for (const auto &invalid : std::vector<std::string>{
                 "", std::string(513, '1'), "9002.1\n", std::string("9\0.1", 4), "not-a-jade-oid",
                 std::string(1, char(255))}) {
            bool rejected = false;
            try {
                inspector.oids.parse(invalid);
            } catch (const std::exception &) {
                rejected = true;
            }
            assert(rejected);
        }
        const auto root = runtime.instances[2];
        const auto cls = inspector.classOf(root);
        const auto expected = inspector.oids.format(root);
        assert(inspector.classInfo(cls).at("oid") == inspector.oids.format(cls));
        const auto detail = inspector.object(root);
        assert(detail.at("oid") == expected);
        std::cout << "Private inspector fixture: schema=" << detail.at("schema").get<std::string>()
                  << " class=" << detail.at("className").get<std::string>() << " oid=" << expected
                  << '\n';
        for (const auto &property : detail.at("properties"))
            if (property.contains("oid") && !property.at("oid").is_null())
                assert(inspector.oids.format(inspector.oids.parse(property.at("oid"))) ==
                       property.at("oid").get<std::string>());
        const auto summary = inspector.identity(root);
        assert(summary.at("oid") == expected);
        assert(summary.at("label").get<std::string>().find(expected) != std::string::npos);
        const auto catalog = inspector.catalog(true);
        for (const auto &schema : catalog.at("schemas")) {
            assert(inspector.oids.format(inspector.oids.parse(schema.at("oid"))) ==
                   schema.at("oid").get<std::string>());
            for (const auto &type : schema.at("classes"))
                assert(inspector.oids.format(inspector.oids.parse(type.at("oid"))) ==
                       type.at("oid").get<std::string>());
        }
        const auto collection = runtime.get(cls, "allProperties").object();
        const auto page =
            inspector.page({{"oid", hex(&collection, sizeof collection)}, {"limit", 1}}, true);
        assert(page.at("oid") == inspector.oids.format(collection));
        for (const auto &item : page.at("items"))
            if (item.contains("oid") && !item.at("oid").is_null())
                assert(inspector.oids.format(inspector.oids.parse(item.at("oid"))) ==
                       item.at("oid").get<std::string>());
        inspector.page({{"classOid", inspector.oids.format(cls)}, {"limit", 1}}, false);
        std::cout
            << "PASS native catalog, object, summary, reference and collection/query identities\n";
        std::cout << "PASS native object-valued identity and unchanged primitive string\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
