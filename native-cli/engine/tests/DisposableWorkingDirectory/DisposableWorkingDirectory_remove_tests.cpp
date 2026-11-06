#include "../fixtures/DisposableWorkingDirectory.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    std::filesystem::path success, failure;
    {
        DisposableWorkingDirectory fixture("jade-working-cleanup-success-");
        success = fixture.path();
        writeText(success / "private" / "system" / "owned.dat", "fixture");
        fixture.remove();
        assert(!std::filesystem::exists(success));
    }
    try {
        DisposableWorkingDirectory fixture("jade-working-cleanup-failure-");
        failure = fixture.path();
        writeText(failure / "private" / "system" / "owned.dat", "fixture");
        throw std::runtime_error("Injected proof assertion failure");
    } catch (const std::runtime_error &) {
    }
    assert(!std::filesystem::exists(failure));
    std::cout << "Owned Working fixture removed on normal completion and assertion exception\n";
}
