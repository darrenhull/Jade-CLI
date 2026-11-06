#pragma once
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

class CatalogPage {
    using json = nlohmann::json;
    struct Entry {
        json value;
        std::string schema, qualifiedName;
    };
    std::vector<Entry> entries_;
    static std::string folded(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    static size_t number(const json &request, const char *key, size_t fallback) {
        if (!request.contains(key))
            return fallback;
        const auto &value = request.at(key);
        if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
            throw std::runtime_error(std::string("Catalog ") + key +
                                     " must be a nonnegative integer.");
        const auto result = value.get<uint64_t>();
        if (result > std::numeric_limits<size_t>::max())
            throw std::runtime_error(std::string("Catalog ") + key +
                                     " exceeds the supported range.");
        return static_cast<size_t>(result);
    }

  public:
    explicit CatalogPage(json snapshot) {
        if (!snapshot.is_object() || !snapshot.contains("schemas") ||
            !snapshot["schemas"].is_array())
            throw std::runtime_error("Catalog snapshot must contain schema metadata.");
        for (auto &schema : snapshot["schemas"]) {
            const auto name = schema.at("name").get<std::string>();
            for (auto &row : schema.at("classes")) {
                if (!row.contains("schema"))
                    row["schema"] = name;
                const auto schemaKey = folded(row.at("schema").get<std::string>());
                const auto qualified = schemaKey + "::" + folded(row.at("name").get<std::string>());
                entries_.push_back({std::move(row), schemaKey, qualified});
            }
        }
    }
    static bool requested(const json &request) {
        return request.contains("query") || request.contains("schema") ||
               request.contains("offset") || request.contains("limit");
    }
    static void validate(const json &request) {
        if (!request.is_object())
            throw std::runtime_error("Catalog paging requires a request object.");
        for (const auto *key : {"query", "schema"})
            if (request.contains(key) && !request.at(key).is_string())
                throw std::runtime_error(std::string("Catalog ") + key +
                                         " filter must be a string.");
        const auto limit = number(request, "limit", 50);
        if (limit < 1 || limit > 100)
            throw std::runtime_error("Catalog page limit must be between 1 and 100.");
        number(request, "offset", 0);
    }
    json page(const json &request) const {
        validate(request);
        const auto query = folded(request.value("query", ""));
        const auto schema = folded(request.value("schema", ""));
        const auto offset = number(request, "offset", 0), limit = number(request, "limit", 50);
        auto rows = json::array();
        size_t total = 0;
        for (const auto &entry : entries_) {
            if ((!schema.empty() && entry.schema != schema) ||
                (!query.empty() && entry.qualifiedName.find(query) == std::string::npos))
                continue;
            if (total >= offset && rows.size() < limit)
                rows.push_back(entry.value);
            ++total;
        }
        const json next = offset < total && rows.size() < total - offset
                              ? json(offset + rows.size())
                              : json(nullptr);
        return {{"readOnly", true},
                {"rows", std::move(rows)},
                {"total", total},
                {"offset", offset},
                {"next_offset", next}};
    }
};
