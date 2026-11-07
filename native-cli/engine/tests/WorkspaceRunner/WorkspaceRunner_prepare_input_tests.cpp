#include "WorkspaceRunner.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    const json request = {
        {"sourceText", "write 42;"},
        {"config", {{"schema", "UserSchema"}, {"workspaceRoots", json::array()}}}};
    bool rejected = false;
    try {
        WorkspaceRunner::validate(request);
    } catch (const std::exception &e) {
        rejected = std::string(e.what()).find("workspace root") != std::string::npos;
    }
    assert(rejected);
    std::cout << "Workspace preparation rejects an empty direct-call root set before native work\n";
}
