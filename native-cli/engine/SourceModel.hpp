#pragma once
#include <string>
#include <vector>
namespace jade::native {
struct ClassDeclaration {
    std::string name, parent;
    bool partial = false;
};
struct MethodDeclaration {
    std::string name, flags;
    size_t line = 1, bodyLine = 1;
    bool unitTest = false, ignored = false;
};
class SourceModel {
  public:
    std::string schema, parentSchema, text, code, source;
    bool completeSchema = false;
    std::vector<ClassDeclaration> classes;
    std::vector<MethodDeclaration> methods;
    std::vector<std::string> identifiers;
    static SourceModel parse(const std::string &text, const std::string &source);
};
} // namespace jade::native
