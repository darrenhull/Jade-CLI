#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
namespace jade::native::broker {
struct BrokerMutexGuard {
    HANDLE handle = nullptr;
    BrokerMutexGuard(const std::wstring &name, SECURITY_ATTRIBUTES *security) {
        handle = CreateMutexW(security, FALSE, name.c_str());
        if (!handle)
            throw std::runtime_error("Cannot create broker state lock (Windows error " +
                                     std::to_string(GetLastError()) + ").");
        const auto waited = WaitForSingleObject(handle, 10000);
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
            CloseHandle(handle);
            handle = nullptr;
            throw std::runtime_error("Timed out acquiring broker state lock.");
        }
    }
    ~BrokerMutexGuard() {
        if (handle) {
            ReleaseMutex(handle);
            CloseHandle(handle);
        }
    }
};
} // namespace jade::native::broker
