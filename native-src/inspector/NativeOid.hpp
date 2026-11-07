#pragma once
#include "runtime.hpp"
#include <functional>

// Jade owns the public representation, lifetime syntax and parsing. Packed
// SDK bytes are accepted only as a compatibility input, never emitted as IDs.
class NativeOid {
    Runtime &runtime;
    std::function<std::string(Param &)> readString;

  public:
    NativeOid(Runtime &value, std::function<std::string(Param &)> reader)
        : runtime(value), readString(std::move(reader)) {}
    std::string format(const Oid &oid) {
        auto object = Param::oid(oid);
        Any result(runtime);
        // Fixed system receiver: never dispatch an application override on oid.
        runtime.send(runtime.instances[1], "getObjectStringForObject", {&object}, result);
        return readString(result);
    }
    Oid parse(const std::string &text) {
        if (text.empty() || text.size() > 512 ||
            std::any_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error(
                "Jade OID must contain 1 to 512 UTF-8 bytes without control characters.");
        // Validate UTF-8 even for compatibility inputs; do not trim or rewrite.
        auto input = Param::string(text);
        if (text.size() == 44 &&
            text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            return parseOid(text);
        auto selector = Param::string("asObject");
        Param result(DSKOBJECTID);
        runtime.check(proc<decltype(&jomCallPrimMethod)>(runtime.jom, "jomCallPrimMethod")(
                          &runtime.process, DSKCSTRING_PRIMITIVE, &input.value, &selector.value,
                          nullptr, &result.value, 0),
                      "Parse native Jade OID");
        const auto parsed = result.object();
        if (nullOid(parsed))
            throw std::runtime_error("Native Jade OID parser returned a null reference.");
        return parsed;
    }
};
