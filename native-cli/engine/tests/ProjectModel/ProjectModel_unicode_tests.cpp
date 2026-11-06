#include "ProjectModel.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    std::filesystem::create_directories(root);
    auto source = root / L"\u00C4\u03A3", same = root / L"\u00E4\u03C3";
    assert(pathKey(source) == pathKey(same));
    assert(contains(source, same / L"system"));
    assert(contains(same, source / L"system"));
    assert(!contains(source, root / L"other"));
    auto sigma = root / L"\u03A3", finalSigma = root / L"\u03C2";
    bool ordinalEqual = CompareStringOrdinal(L"\u03A3", 1, L"\u03C2", 1, TRUE) == CSTR_EQUAL;
    assert((pathKey(sigma) == pathKey(finalSigma)) == ordinalEqual);
    assert(contains(sigma, finalSigma / L"working") == ordinalEqual);
    std::filesystem::remove_all(root);
    std::cout << "ProjectModel Unicode Windows path identity and overlap\n";
}
