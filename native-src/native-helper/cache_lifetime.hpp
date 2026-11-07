#pragma once
#include <utility>
#include <string>
namespace jade {
template<class Json, class Read>
void capture_type_lifetime(Json& result, Read read) {
    for (const auto& [field, key] : {
             std::pair{"Final", "final"},
             {"TransientAllowed", "transient_allowed"},
             {"PersistentAllowed", "persistent_allowed"},
             {"SharedTransientAllowed", "shared_transient_allowed"},
             {"SubclassPersistentAllowed", "subclass_persistent_allowed"},
             {"SubclassSharedTransientAllowed", "subclass_shared_transient_allowed"},
             {"SubclassTransientAllowed", "subclass_transient_allowed"}}) {
        const auto entry = std::string("?get") + field + "@DskType@@UEBAHPEA_NI@Z";
        result[key] = bool(read(entry.c_str()));
    }
}
}
