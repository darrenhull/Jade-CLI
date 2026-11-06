#include "PrivateFile.hpp"
#include <sddl.h>
#include <aclapi.h>
#include "PrivateSecurity.hpp"
namespace jade::native {
void PrivateFile::write(const std::filesystem::path &file, const std::string &contents) {
    std::filesystem::create_directories(file.parent_path());
    PrivateSecurity security;
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, &security.attributes, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create private native configuration: " +
                                 utf8(file.wstring()));
    size_t offset = 0;
    while (offset < contents.size()) {
        DWORD count = 0;
        if (!WriteFile(handle, contents.data() + offset,
                       DWORD(std::min<size_t>(contents.size() - offset, 65536)), &count, nullptr) ||
            !count) {
            CloseHandle(handle);
            std::error_code error;
            std::filesystem::remove(file, error);
            throw std::runtime_error("Cannot write private native configuration.");
        }
        offset += count;
    }
    CloseHandle(handle);
}
void PrivateFile::remove(const std::filesystem::path &file) {
    if (file.empty())
        return;
    std::error_code error;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        std::filesystem::remove(file, error);
        if (!error || error == std::errc::no_such_file_or_directory)
            return;
        Sleep(25U << attempt);
    }
    throw std::runtime_error("Cannot remove native session configuration after process exit: " +
                             utf8(file.wstring()) + " (" + error.message() + ").");
}
void PrivateFile::protectDirectory(const std::filesystem::path &directory) {
    std::filesystem::create_directories(directory);
    PrivateSecurity security;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(security.descriptor, &present, &dacl, &defaulted) || !present)
        throw std::runtime_error("Private native directory ACL missing.");
    auto result =
        SetNamedSecurityInfoW(const_cast<wchar_t *>(directory.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, dacl, nullptr);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Cannot protect native working directory (Windows " +
                                 std::to_string(result) + ").");
}
} // namespace jade::native
