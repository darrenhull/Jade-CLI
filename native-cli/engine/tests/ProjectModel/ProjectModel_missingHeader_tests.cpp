#include "ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId(), workspace = root / "workspace",
         outside = root / "external";
    writeText(workspace / "Demo.scm",
              "schemaDefinition\nDemo subschemaOf Base partialDefinition;\n");
    writeText(workspace / "Test.cls",
              "schemaDefinition\nDemo subschemaOf Base partialDefinition;\ntypeHeaders\nTest "
              "subclassOf Object transient;\ntypeDefinitions\nTest "
              "completeDefinition\n(\nattributeDefinitions\nvalue: BaseClass;\n)\ntypeSources\n");
    writeText(outside / "BaseClass.cls",
              "schemaDefinition\nBase subschemaOf RootSchema "
              "partialDefinition;\ntypeHeaders\nBaseClass subclassOf Object "
              "transient;\ntypeDefinitions\nBaseClass completeDefinition\n(\n)\ntypeSources\n");
    auto selected = ProjectModel::scan({workspace}, {outside / "BaseClass.cls"})
                        .dependencies(workspace / "Test.cls");
    assert(!selected.schemas.empty() && !selected.loose.empty());
    bool rejected = false;
    try {
        selected.inventory();
    } catch (const std::exception &e) {
        auto message = std::string(e.what());
        rejected =
            message.find("Base") != std::string::npos && message.find(".scm") != std::string::npos;
    }
    assert(rejected);
    auto clsOnly = ProjectModel::scan({outside});
    assert(clsOnly.schemas.empty() && clsOnly.inventory().size() == 1);
    std::filesystem::remove_all(root);
    std::cout
        << "ProjectModel missing ancestor header fails closed; standalone CLS remains supported\n";
}
