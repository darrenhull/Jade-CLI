#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>
#include <stdexcept>

class BrokerTestPipe {
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::string pending_;
    static std::string utf8(const std::wstring &text) {
        const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0,
                                              nullptr, nullptr);
        std::string bytes(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), size, nullptr,
                            nullptr);
        return bytes;
    }

  public:
    explicit BrokerTestPipe(const std::filesystem::path &root) {
        HANDLE token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            throw std::runtime_error("Cannot identify fixture user.");
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> user(size);
        if (!GetTokenInformation(token, TokenUser, user.data(), size, &size)) {
            CloseHandle(token);
            throw std::runtime_error("Cannot read fixture user.");
        }
        CloseHandle(token);
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid))
            throw std::runtime_error("Cannot format fixture SID.");
        auto directory = std::filesystem::absolute(root).lexically_normal().wstring();
        std::transform(directory.begin(), directory.end(), directory.begin(), towlower);
        const auto identity = utf8(directory) + utf8(sid);
        LocalFree(sid);
        unsigned char hash[32];
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                       reinterpret_cast<PUCHAR>(const_cast<char *>(identity.data())),
                       ULONG(identity.size()), hash, sizeof(hash)))
            throw std::runtime_error("Cannot hash fixture pipe.");
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring name = L"\\\\.\\pipe\\jade-native-";
        for (unsigned i = 0; i < 16; ++i) {
            name += hex[hash[i] >> 4];
            name += hex[hash[i] & 15];
        }
        pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open the isolated fixture broker pipe.");
    }
    ~BrokerTestPipe() {
        if (pipe_ != INVALID_HANDLE_VALUE)
            CloseHandle(pipe_);
    }
    bool write(const std::string &text) {
        size_t at = 0;
        while (at < text.size()) {
            DWORD count = 0;
            if (!WriteFile(pipe_, text.data() + at,
                           DWORD(std::min<size_t>(65536, text.size() - at)), &count, nullptr) ||
                !count)
                return false;
            at += count;
        }
        return true;
    }
    bool closedWithin(unsigned milliseconds) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < milliseconds) {
            DWORD bytes = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &bytes, nullptr))
                return true;
            Sleep(5);
        }
        return false;
    }
    std::string line(unsigned milliseconds = 3000) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < milliseconds) {
            const auto end = pending_.find('\n');
            if (end != std::string::npos) {
                auto result = pending_.substr(0, end);
                pending_.erase(0, end + 1);
                return result;
            }
            DWORD bytes = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &bytes, nullptr))
                throw std::runtime_error("Fixture broker disconnected before a response.");
            if (bytes) {
                char text[8192];
                DWORD count = 0;
                if (!ReadFile(pipe_, text, std::min<DWORD>(bytes, sizeof(text)), &count, nullptr))
                    throw std::runtime_error("Fixture broker read failed.");
                pending_.append(text, count);
            } else
                Sleep(5);
        }
        throw std::runtime_error("Fixture broker response timeout.");
    }
};
