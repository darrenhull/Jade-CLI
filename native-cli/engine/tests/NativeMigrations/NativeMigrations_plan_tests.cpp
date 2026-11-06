#include "NativeMigrations.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto ops = jade::native::NativeMigrations::parse(
        "JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nRename Class Demo::Pet Fish\n",
        "rename.jcf");
    assert(ops.size() == 1);
    assert(ops[0].oldName == "Pet");
    assert(ops[0].newName == "Fish");
    bool rejected = false;
    try {
        jade::native::NativeMigrations::parse(
            "JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nDelete Schema Demo\n",
            "bad.jcf");
    } catch (...) {
        rejected = true;
    }
    assert(rejected);
    auto pending = jade::native::NativeMigrations::plan(ops, {"Demo::Pet"}, {"Demo::Fish"});
    assert(pending.size() == 1);
    assert(jade::native::NativeMigrations::plan(ops, {"Demo::Fish"}, {"Demo::Fish"}).empty());
    std::cout << "NativeMigrations contracts passed\n";
}
