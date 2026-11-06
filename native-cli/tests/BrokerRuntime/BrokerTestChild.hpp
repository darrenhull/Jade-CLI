#pragma once
#include "../../broker/BrokerProcess.hpp"
#include <windows.h>
#include <filesystem>
#include <stdexcept>

class BrokerTestChild {
    HANDLE process_ = nullptr, input_ = nullptr, output_ = nullptr;
    std::string outputPending_;

  public:
    BrokerTestChild(const std::filesystem::path &executable,
                    const std::vector<std::wstring> &arguments) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE childInput = nullptr, childOutput = nullptr;
        if (!CreatePipe(&childInput, &input_, &sa, 0) ||
            !CreatePipe(&output_, &childOutput, &sa, 0))
            throw std::runtime_error("Cannot create fixture pipes.");
        SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childInput;
        startup.hStdOutput = childOutput;
        startup.hStdError = childOutput;
        PROCESS_INFORMATION child{};
        auto command = jade::native::quoteWindowsArgument(executable.wstring());
        for (auto &arg : arguments)
            command += L" " + jade::native::quoteWindowsArgument(arg);
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child))
            throw std::runtime_error("Cannot launch fixture client.");
        process_ = child.hProcess;
        CloseHandle(child.hThread);
        CloseHandle(childInput);
        CloseHandle(childOutput);
    }
    ~BrokerTestChild() {
        if (process_) {
            if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
                TerminateProcess(process_, 130);
            WaitForSingleObject(process_, 5000);
            CloseHandle(process_);
        }
        if (input_)
            CloseHandle(input_);
        if (output_)
            CloseHandle(output_);
    }
    void write(const std::string &text) {
        DWORD count = 0;
        if (!WriteFile(input_, text.data(), DWORD(text.size()), &count, nullptr) ||
            count != text.size())
            throw std::runtime_error("Cannot write fixture request.");
    }
    bool wait(unsigned milliseconds) {
        return WaitForSingleObject(process_, milliseconds) == WAIT_OBJECT_0;
    }
    int code() {
        DWORD code;
        GetExitCodeProcess(process_, &code);
        return int(code);
    }
    void stop() {
        TerminateProcess(process_, 130);
        WaitForSingleObject(process_, 5000);
    }
    bool waitForOutput(unsigned milliseconds) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < milliseconds) {
            DWORD bytes = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &bytes, nullptr))
                return false;
            if (bytes)
                return true;
            Sleep(5);
        }
        return false;
    }
    bool waitForText(const std::string &text, unsigned milliseconds) {
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < milliseconds) {
            if (outputPending_.find(text) != std::string::npos)
                return true;
            DWORD available = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr))
                return false;
            if (available) {
                char bytes[8192];
                DWORD count = 0;
                if (!ReadFile(output_, bytes, std::min<DWORD>(available, sizeof(bytes)), &count,
                              nullptr))
                    return false;
                outputPending_.append(bytes, count);
                if (outputPending_.size() > 256 * 1024)
                    throw std::runtime_error("Fixture output exceeds its bounded wait budget.");
            } else
                Sleep(5);
        }
        return outputPending_.find(text) != std::string::npos;
    }
};
