#include "../../CatalogPage.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid catalog paging input was accepted.");
}
} // namespace
int main() {
    try {
        using json = nlohmann::json;
        json snapshot = {{"readOnly", true}, {"schemas", json::array()}};
        for (int schema = 0; schema < 2; ++schema) {
            auto classes = json::array();
            for (int i = schema * 15000; i < (schema + 1) * 15000; ++i)
                classes.push_back({{"name", "Pet" + std::to_string(i)},
                                   {"schema", schema ? "ZooB" : "ZooA"},
                                   {"oid", std::string(44, 'a')},
                                   {"metadata", std::string(512, 'x')}});
            snapshot["schemas"].push_back(
                {{"name", schema ? "ZooB" : "ZooA"}, {"classes", std::move(classes)}});
        }
        require(snapshot.dump().size() > 8 * 1024 * 1024,
                "Large catalog fixture does not exercise the full-response transport limit.");
        require(!CatalogPage::requested(json::object()),
                "Unpaged requests must retain the legacy catalog shape.");
        for (const auto *key : {"query", "schema", "offset", "limit"})
            require(CatalogPage::requested({{key, nullptr}}),
                    "A paging/filter field did not select the bounded response.");
        CatalogPage catalog(std::move(snapshot));
        auto first = catalog.page({{"limit", 1}});
        require(first.at("readOnly") == true && first.at("total") == 30000 &&
                    first.at("offset") == 0,
                "Catalog page summary is incorrect.");
        require(first.at("rows").size() == 1 && first.at("next_offset") == 1 &&
                    first.dump().size() < 2048,
                "Large catalog was serialized instead of its bounded page.");
        auto filtered = catalog.page({{"schema", "zoob"}, {"query", "pEt29999"}, {"limit", 1}});
        require(filtered.at("total") == 1 && filtered.at("rows").size() == 1 &&
                    filtered.at("rows")[0].at("name") == "Pet29999",
                "Schema and query filtering lost the matching class.");
        require(filtered.at("rows")[0].at("schema") == "ZooB" &&
                    filtered.at("rows")[0].at("oid") == std::string(44, 'a'),
                "Catalog page lost schema identity or native class metadata.");
        require(filtered.at("next_offset").is_null(),
                "A completed filtered page advertises another page.");
        auto qualified = catalog.page({{"query", "ZOOB::pet29999"}});
        require(qualified.at("total") == 1, "Schema-qualified class search failed.");
        auto schemaOnly = catalog.page({{"schema", "zooa"}, {"limit", 100}});
        require(schemaOnly.at("total") == 15000 && schemaOnly.at("rows").size() == 100,
                "Schema-only catalog filter failed.");
        auto last = catalog.page({{"offset", 29999}, {"limit", 1}});
        require(last.at("rows").size() == 1 && last.at("next_offset").is_null(),
                "Last catalog page is incorrect.");
        auto beyond = catalog.page({{"offset", 30001}, {"limit", 1}});
        require(beyond.at("rows").empty() && beyond.at("total") == 30000 &&
                    beyond.at("next_offset").is_null(),
                "Offset beyond the result was not empty.");
        require(catalog.page({{"query", "absent"}}).at("total") == 0,
                "Missing catalog query did not return an empty page.");
        for (const auto &invalid :
             {json{{"limit", 0}}, json{{"limit", 101}}, json{{"limit", "1"}}, json{{"offset", -1}},
              json{{"offset", 1.5}}, json{{"query", 1}}, json{{"schema", json::array()}}})
            rejects([&] { CatalogPage::validate(invalid); });
        CatalogPage empty({{"schemas", json::array()}});
        require(empty.page({{"limit", 1}}).at("rows").empty(), "Empty catalog page failed.");
        std::cout << "CatalogPage large snapshot, legacy selection, filtering, paging, native "
                     "identity and bounds: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
