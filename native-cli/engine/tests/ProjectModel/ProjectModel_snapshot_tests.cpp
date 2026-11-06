#include "ProjectModel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto file = root / "Pet.cls";
    std::string text =
        "schemaDefinition\nDemo subschemaOf RootSchema partialDefinition;\ntypeHeaders\nPet "
        "subclassOf Object transient;\ntypeDefinitions\nPet "
        "completeDefinition\n(\nattributeDefinitions\ntypeSources: String;\nvalue: "
        "String;\njadeMethodDefinitions\ngetValue():String;\n)\ntypeSources\nPet "
        "(\njadeMethodSources\ngetValue\n{\ngetValue():String;\nbegin\nreturn "
        "self.value;\nend;\n}\n)\n";
    writeText(file, text);
    auto original = ProjectModel::scan({root}).loose[0];
    auto body = text;
    body.replace(body.find("return self.value;"), 18, "return \"Value\";");
    writeText(file, body);
    auto changedBody = ProjectModel::scan({root}).loose[0];
    assert(original.hash != changedBody.hash && original.shape == changedBody.shape);
    auto signature = text;
    signature.replace(signature.find("getValue():String;\nbegin"), 18,
                      "getValue():String updating;");
    writeText(file, signature);
    auto changedSignature = ProjectModel::scan({root}).loose[0];
    assert(original.shape != changedSignature.shape);
    auto property = text;
    property.replace(property.find("value: String;"), 14, "value: Integer;");
    writeText(file, property);
    auto changedProperty = ProjectModel::scan({root}).loose[0];
    assert(original.shape != changedProperty.shape);
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel body-only vs signature/property shape invalidation passed\n";
}
