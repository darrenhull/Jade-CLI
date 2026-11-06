#include "PrivateFile.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    using namespace jade::native;
    auto root = std::filesystem::temp_directory_path() / uniqueId();
    auto file = root / "secret.json";
    PrivateFile::write(file, "test secret");
    assert(readText(file) == "test secret");
    HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(lock != INVALID_HANDLE_VALUE);
    bool reported = false;
    try {
        PrivateFile::remove(file);
    } catch (...) {
        reported = true;
    }
    assert(reported && std::filesystem::exists(file));
    CloseHandle(lock);
    PrivateFile::remove(file);
    assert(!std::filesystem::exists(file));
    PrivateFile::remove(file);
    std::filesystem::remove_all(root);
    std::cout << "PrivateFile locked cleanup reports failure and retries successfully\n";
}
