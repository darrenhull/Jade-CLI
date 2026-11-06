#include "SourceModel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
    std::string prefix =
        "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
        "subclassOf Object transient;\ntypeDefinitions\nPet "
        "completeDefinition\n(\n)\ntypeSources\nPet (\njadeMethodSources\nworks\n{\nworks()";
    auto malformed = jade::native::SourceModel::parse(
        prefix + std::string(512 * 1024, ' ') + "\nbegin\nend\n}\n)", "large.cls");
    assert(malformed.schema == "Demo");
    assert(malformed.methods.empty());
    auto valid = prefix + " unitTest;\nbegin\nend;\n}\n)\n";
    auto start = std::chrono::steady_clock::now();
    size_t count = 0;
    for (int i = 0; i < 30000; ++i) {
        auto m = jade::native::SourceModel::parse(valid, "Pet.cls");
        count += m.methods.size();
    }
    assert(count == 30000);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();
    std::cout << "SourceModel 30,000 class parses: " << ms
              << " ms; 512KiB malformed signature handled\n";
}
