#include "PrivateSecurity.hpp"
#include <sddl.h>
#include <stdexcept>
namespace jade::native {
PrivateSecurity::PrivateSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot read native session user.");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(token);
        throw std::runtime_error("Cannot read native session user SID.");
    }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        throw std::runtime_error("Cannot format native session SID.");
    std::wstring sddl = L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        throw std::runtime_error("Cannot construct private native file permissions.");
    attributes = {sizeof(attributes), descriptor, FALSE};
}
PrivateSecurity::~PrivateSecurity() {
    if (descriptor)
        LocalFree(descriptor);
}
} // namespace jade::native
