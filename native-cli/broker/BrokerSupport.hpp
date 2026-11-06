#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "BrokerRuntime.hpp"
#include "BrokerProcess.hpp"
#include "BrokerMutexGuard.hpp"
#include "BrokerStatePaths.hpp"
#include "../engine/PrivateSecurity.hpp"
#include <aclapi.h>
#include <algorithm>
#include <bcrypt.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sddl.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <windows.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace jade::native::broker {
using json = nlohmann::json;
inline constexpr size_t maximumPacket = 16 * 1024 * 1024;
inline std::runtime_error windowsError(const char *operation) {
    return std::runtime_error(std::string(operation) + " (Windows error " +
                              std::to_string(GetLastError()) + ").");
}
inline std::string hex(const unsigned char *data, size_t size) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += alphabet[data[i] >> 4];
        out += alphabet[data[i] & 15];
    }
    return out;
}
inline std::string randomToken() {
    unsigned char value[32];
    if (BCryptGenRandom(nullptr, value, sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        throw std::runtime_error("Cannot generate broker authentication token.");
    return hex(value, sizeof(value));
}
inline std::string sha256(const std::string &bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
        throw std::runtime_error("Cannot initialise SHA-256.");
    auto finish = [&] {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())),
                       static_cast<ULONG>(bytes.size()), 0) ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0)) {
        finish();
        throw std::runtime_error("Cannot hash broker identity.");
    }
    finish();
    return hex(digest, sizeof(digest));
}
inline std::wstring currentSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw windowsError("Cannot identify broker user");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> data(size);
    if (!GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
        CloseHandle(token);
        throw windowsError("Cannot read broker user");
    }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid))
        throw windowsError("Cannot format broker user");
    std::wstring result = sid;
    LocalFree(sid);
    return result;
}

inline BrokerStatePaths prepare(const BrokerOptions &supplied) {
    BrokerStatePaths p;
    p.options = supplied;
    if (p.options.executable.empty()) {
        std::vector<wchar_t> value(32768);
        DWORD n = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
        if (!n || n == value.size())
            throw windowsError("Cannot locate native CLI");
        p.options.executable = std::wstring(value.data(), n);
    }
    if (p.options.stateDirectory.empty()) {
        wchar_t value[32768];
        DWORD n = GetEnvironmentVariableW(L"JADE_TOOLS_STATE_DIRECTORY", value, 32768);
        if (n && n < 32768)
            p.options.stateDirectory = value;
        else {
            n = GetEnvironmentVariableW(L"LOCALAPPDATA", value, 32768);
            if (!n || n >= 32768)
                throw std::runtime_error("LOCALAPPDATA is required for private broker state.");
            p.options.stateDirectory =
                std::filesystem::path(value) / L"JadeTools" / L"broker-v2-native";
        }
    }
    p.root = std::filesystem::absolute(p.options.stateDirectory).lexically_normal();
    std::filesystem::create_directories(p.root);
    if (GetFileAttributesW(p.root.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Private broker state cannot be a reparse point.");
    PrivateSecurity security;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(security.descriptor, &present, &dacl, &defaulted) || !present)
        throw std::runtime_error("Private broker ACL is missing.");
    DWORD acl =
        SetNamedSecurityInfoW(const_cast<wchar_t *>(p.root.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, dacl, nullptr);
    if (acl != ERROR_SUCCESS)
        throw std::runtime_error("Cannot protect private broker state (Windows error " +
                                 std::to_string(acl) + ").");
    std::wstring identity = p.root.wstring();
    std::transform(identity.begin(), identity.end(), identity.begin(), towlower);
    const auto key = wide(sha256(utf8(identity) + utf8(currentSid())).substr(0, 32));
    p.pipe = L"\\\\.\\pipe\\jade-native-" + key;
    p.mutex = L"Local\\jade-native-start-" + key;
    BrokerMutexGuard tokenLock(p.mutex + L"-token", &security.attributes);
    p.token = p.root / L"token";
    p.owner = p.root / L"owner.json";
    HANDLE file = CreateFileW(p.token.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                              &security.attributes, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        auto token = randomToken();
        DWORD count = 0;
        if (!WriteFile(file, token.data(), static_cast<DWORD>(token.size()), &count, nullptr) ||
            count != token.size()) {
            CloseHandle(file);
            throw windowsError("Cannot write broker token");
        }
        FlushFileBuffers(file);
        CloseHandle(file);
    } else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
        throw windowsError("Cannot create broker token");
    file = CreateFileW(p.token.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw windowsError("Cannot read broker token");
    BY_HANDLE_FILE_INFORMATION information{};
    bool valid = GetFileInformationByHandle(file, &information) &&
                 !(information.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                 information.nFileSizeHigh == 0 && information.nFileSizeLow == 64;
    char token[65]{};
    DWORD count = 0;
    if (!valid || !ReadFile(file, token, 64, &count, nullptr) || count != 64) {
        CloseHandle(file);
        throw std::runtime_error("Invalid broker authentication token.");
    }
    CloseHandle(file);
    p.secret.assign(token, 64);
    if (p.options.nativeDirectory.empty())
        p.options.nativeDirectory = p.options.executable.parent_path() / L"native";
    if (p.options.fingerprint.empty()) {
        std::ostringstream bytes;
        for (const auto &file :
             {p.options.executable, p.options.nativeDirectory / L"jade-test-helper.exe",
              p.options.nativeDirectory / L"jade-inspect-helper.exe",
              p.options.nativeDirectory / L"jade-debug-helper.exe",
              p.options.nativeDirectory / L"jade-native-helper.exe"}) {
            if (file != p.options.executable && !std::filesystem::exists(file))
                continue;
            std::ifstream in(file, std::ios::binary);
            if (!in)
                throw std::runtime_error("Cannot fingerprint native CLI runtime input.");
            bytes << utf8(file.filename().wstring()) << '\n' << in.rdbuf();
            if (!in.good() && !in.eof())
                throw std::runtime_error("Cannot fingerprint native CLI runtime input.");
        }
        p.options.fingerprint = sha256(bytes.str());
    }
    return p;
}
inline bool equalSecret(const std::string &a, const std::string &b) {
    unsigned difference = static_cast<unsigned>(a.size() ^ b.size());
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
        difference |= static_cast<unsigned>((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0));
    return difference == 0;
}
inline bool alive(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
inline HANDLE openPipe(const BrokerStatePaths &p) {
    return CreateFileW(p.pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                       nullptr);
}
inline HANDLE ensureBroker(const BrokerStatePaths &p) {
    HANDLE pipe = openPipe(p);
    if (pipe != INVALID_HANDLE_VALUE)
        return pipe;
    PrivateSecurity security;
    HANDLE lock = CreateMutexW(&security.attributes, FALSE, p.mutex.c_str());
    if (!lock)
        throw windowsError("Cannot acquire broker startup lock");
    DWORD waited = WaitForSingleObject(lock, 10000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
        CloseHandle(lock);
        throw std::runtime_error("Timed out acquiring broker startup lock.");
    }
    try {
        pipe = openPipe(p);
        if (pipe == INVALID_HANDLE_VALUE) {
            bool launch = true;
            if (std::filesystem::exists(p.owner)) {
                std::ifstream input(p.owner);
                json previous;
                input >> previous;
                if (previous.contains("brokerPID") && alive(previous["brokerPID"].get<DWORD>()))
                    launch = false;
                else
                    for (auto pid : previous.value("nativePIDs", json::array()))
                        if (alive(pid.get<DWORD>()))
                            throw std::runtime_error(
                                "A native Jade process survived the previous broker. Close its "
                                "owned session before restarting.");
            }
            if (launch) {
                std::wstring command = quoteWindowsArgument(p.options.executable.wstring()) +
                                       L" broker --state-dir " +
                                       quoteWindowsArgument(p.root.wstring());
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(p.options.executable.c_str(), command.data(), nullptr, nullptr,
                                    FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                                    nullptr, &startup, &process))
                    throw windowsError("Cannot start native broker");
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
            for (int i = 0; i < 200 && pipe == INVALID_HANDLE_VALUE; ++i) {
                Sleep(25);
                pipe = openPipe(p);
            }
            if (pipe == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Native broker did not become available.");
        }
    } catch (...) {
        ReleaseMutex(lock);
        CloseHandle(lock);
        throw;
    }
    ReleaseMutex(lock);
    CloseHandle(lock);
    return pipe;
}
inline void writePipe(HANDLE pipe, const std::string &bytes) {
    size_t offset = 0;
    const auto started = GetTickCount64();
    while (offset < bytes.size()) {
        DWORD count = 0;
        if (!WriteFile(pipe, bytes.data() + offset,
                       static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 65536)), &count,
                       nullptr)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_DATA && error != ERROR_PIPE_BUSY)
                throw windowsError("Broker pipe write failed");
        }
        if (!count) {
            if (GetTickCount64() - started > 2000)
                throw std::runtime_error("Broker client stopped reading its responses.");
            Sleep(5);
            continue;
        }
        offset += count;
    }
}
inline bool readLine(HANDLE pipe, std::string &pending, std::string &line,
                     const std::atomic_bool *stopped = nullptr, unsigned timeout = 0) {
    const auto started = GetTickCount64();
    for (;;) {
        auto end = pending.find('\n');
        if (end != std::string::npos) {
            line = pending.substr(0, end);
            pending.erase(0, end + 1);
            return true;
        }
        if ((stopped && *stopped) || (timeout && GetTickCount64() - started > timeout))
            return false;
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
            return false;
        if (!available) {
            Sleep(5);
            continue;
        }
        char bytes[8192];
        DWORD count = 0;
        if (!ReadFile(pipe, bytes, std::min<DWORD>(available, sizeof(bytes)), &count, nullptr) ||
            !count)
            return false;
        pending.append(bytes, count);
        if (pending.size() > maximumPacket)
            throw std::runtime_error("Broker packet exceeds 16 MiB limit.");
    }
}
} // namespace jade::native::broker
