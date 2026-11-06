#include "NativeMigrations.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto ops =
        NativeMigrations::parse("JadeCommandFile\nJadeVersionNumber 25.0.02\nCommands\nRename "
                                "Class Demo::A B\nRename Class Demo::B C\n",
                                "chain.jcf");
    auto pending = NativeMigrations::plan(ops, {"Demo::A"}, {"Demo::C"});
    assert(pending.size() == 2);
    assert(NativeMigrations::plan(ops, {"Demo::C"}, {"Demo::C"}).empty());
    bool collision = false;
    try {
        NativeMigrations::plan(ops, {"Demo::A", "Demo::B"}, {"Demo::C"});
    } catch (...) {
        collision = true;
    }
    assert(collision);
    bool missing = false;
    try {
        NativeMigrations::plan(ops, {"Demo::A"}, {"Demo::Other"});
    } catch (...) {
        missing = true;
    }
    assert(missing);
    bool invalid = false;
    try {
        NativeMigrations::parse("JadeCommandFile\nCommands\nRename Class Demo::A B\n",
                                "noversion.jcf");
    } catch (...) {
        invalid = true;
    }
    assert(invalid);
    std::cout << "NativeMigrations chain, idempotence, collision and invalid input edges passed\n";
}
