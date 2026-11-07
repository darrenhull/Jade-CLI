#include "NativeProcess.hpp"
#include "NativeTimeout.hpp"
#include <chrono>
#include <stdexcept>
namespace jade::native {
std::string utf8(const std::wstring &value) {
    if (value.empty())
        return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (!count)
        throw std::runtime_error("Invalid UTF-16 text.");
    std::string r(count, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), r.data(),
                        count, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string &value) {
    if (value.empty())
        return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()),
                                    nullptr, 0);
    if (!count)
        throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring r(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), r.data(),
                        count);
    return r;
}
static void close(HANDLE &h) {
    if (h && h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    h = nullptr;
}
NativeProcess::~NativeProcess() {
    stop();
    close(input_);
    close(out_);
    close(err_);
    close(process_);
    close(job_);
}
std::wstring NativeProcess::quote(const std::wstring &value) {
    std::wstring r = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            r.append(slashes * 2 + 1, L'\\');
        else
            r.append(slashes, L'\\');
        slashes = 0;
        r += c;
    }
    r.append(slashes * 2, L'\\');
    return r + L'"';
}
void NativeProcess::start(const std::filesystem::path &exe, const std::vector<std::wstring> &args,
                          const std::filesystem::path &cwd) {
    if (process_)
        throw std::runtime_error("Native process already started.");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readIn = nullptr, writeOut = nullptr, writeErr = nullptr;
    auto cleanup = [&] {
        close(readIn);
        close(writeOut);
        close(writeErr);
    };
    if (!CreatePipe(&readIn, &input_, &sa, 0) || !CreatePipe(&out_, &writeOut, &sa, 0) ||
        !CreatePipe(&err_, &writeErr, &sa, 0)) {
        cleanup();
        throw std::runtime_error("Cannot create native process pipes.");
    }
    SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = quote(exe.wstring());
    for (auto &a : args)
        command +=
            L" " +
            (a.empty() || a.find_first_of(L" \t\n\v\"") != std::wstring::npos ? quote(a) : a);
    STARTUPINFOEXW extended{};
    extended.StartupInfo.cb = sizeof(extended);
    auto &si = extended.StartupInfo;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readIn;
    si.hStdOutput = writeOut;
    si.hStdError = writeErr;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    extended.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(extended.lpAttributeList, 1, 0, &attributeBytes)) {
        cleanup();
        throw std::runtime_error("Cannot initialize native handle isolation.");
    }
    HANDLE inherited[] = {readIn, writeOut, writeErr};
    if (!UpdateProcThreadAttribute(extended.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(extended.lpAttributeList);
        cleanup();
        throw std::runtime_error("Cannot isolate native child handles.");
    }
    PROCESS_INFORMATION pi{};
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) {
        cleanup();
        throw std::runtime_error("Cannot create owned process job.");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        cleanup();
        throw std::runtime_error("Cannot configure owned process job.");
    }
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        DeleteProcThreadAttributeList(extended.lpAttributeList);
        cleanup();
        throw std::runtime_error("Cannot start native executable: " + utf8(exe.wstring()) +
                                 " (Win32 " + std::to_string(GetLastError()) + ").");
    }
    DeleteProcThreadAttributeList(extended.lpAttributeList);
    process_ = pi.hProcess;
    if (!AssignProcessToJobObject(job_, process_)) {
        TerminateProcess(process_, 2);
        CloseHandle(pi.hThread);
        cleanup();
        throw std::runtime_error("Cannot establish native process ownership.");
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    cleanup();
}
void NativeProcess::write(const std::string &text) {
    std::lock_guard lock(inputMutex_);
    if (!input_)
        throw std::runtime_error("Native input pipe closed.");
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD count = 0;
        if (!WriteFile(input_, text.data() + offset,
                       DWORD(std::min<size_t>(text.size() - offset, 65536)), &count, nullptr) ||
            !count)
            throw std::runtime_error("Native input pipe closed.");
        offset += count;
    }
}
void NativeProcess::endInput() {
    std::lock_guard lock(inputMutex_);
    close(input_);
}
unsigned long NativeProcess::processId() const {
    return process_ ? GetProcessId(process_) : 0;
}
int NativeProcess::exitCode() const {
    if (!process_ || alive())
        throw std::runtime_error("Native process has not exited.");
    DWORD code = 2;
    if (!GetExitCodeProcess(process_, &code))
        throw std::runtime_error("Cannot obtain native process exit code.");
    return static_cast<int>(code);
}
bool NativeProcess::alive() const {
    return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}
bool NativeProcess::waitExit(unsigned milliseconds) {
    return !process_ || WaitForSingleObject(process_, milliseconds) == WAIT_OBJECT_0;
}
void NativeProcess::stop() {
    if (alive()) {
        if (!TerminateJobObject(job_, 130))
            TerminateProcess(process_, 130);
        WaitForSingleObject(process_, INFINITE);
    }
    // Terminate before taking inputMutex_: a writer can be blocked inside
    // WriteFile while holding it, and child exit is what releases that write.
    endInput();
}
void NativeProcess::pump(const Output &stdoutOutput, const Output &stderrOutput) {
    auto read = [](HANDLE pipe, std::string &pending, const Output &sink) {
        DWORD available = 0;
        while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[8192];
            DWORD count = 0;
            if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &count,
                          nullptr) ||
                !count)
                break;
            pending.append(buffer, count);
            size_t complete = pending.size();
            if (complete) {
                size_t lead = complete;
                while (lead && ((static_cast<unsigned char>(pending[lead - 1]) & 0xC0) == 0x80))
                    --lead;
                if (lead) {
                    --lead;
                    auto c = static_cast<unsigned char>(pending[lead]);
                    size_t length = c < 0x80             ? 1
                                    : (c & 0xE0) == 0xC0 ? 2
                                    : (c & 0xF0) == 0xE0 ? 3
                                    : (c & 0xF8) == 0xF0 ? 4
                                                         : 1;
                    if (complete - lead < length)
                        complete = lead;
                }
            }
            if (complete) {
                auto text = pending.substr(0, complete);
                pending.erase(0, complete);
                if (sink)
                    sink(text);
            }
        }
    };
    read(out_, outPending_, stdoutOutput);
    read(err_, errPending_, stderrOutput);
}
std::string NativeProcess::line(std::atomic_bool *cancelled, unsigned timeout,
                                const Output &output) {
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        auto end = lines_.find('\n');
        if (end != std::string::npos) {
            auto line = lines_.substr(0, end);
            lines_.erase(0, end + 1);
            return line;
        }
        if (cancelled && cancelled->load()) {
            stop();
            throw std::runtime_error("Operation cancelled.");
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout)) {
            stop();
            throw NativeTimeout("Native request timed out.");
        }
        pump(
            [&](auto &s) {
                lines_ += s;
                if (lines_.size() > 8 * 1024 * 1024)
                    throw std::runtime_error("Native response exceeds size limit.");
            },
            output);
        if (!alive() && lines_.find('\n') == std::string::npos)
            throw std::runtime_error("Native process exited before completing its response.");
        Sleep(5);
    }
}
ProcessResult NativeProcess::run(const std::filesystem::path &exe,
                                 const std::vector<std::wstring> &args,
                                 const std::filesystem::path &cwd, std::atomic_bool *cancelled,
                                 unsigned timeout, const Output &output, bool closeInput) {
    start(exe, args, cwd);
    if (closeInput) endInput();
    ProcessResult result;
    auto beginning = std::chrono::steady_clock::now();
    auto sink = [&](const std::string &s) {
        if (result.output.size() + s.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Native output exceeds size limit.");
        result.output += s;
        if (output)
            output(s);
    };
    while (alive()) {
        pump(sink, sink);
        if (cancelled && cancelled->load()) {
            result.cancelled = true;
            stop();
            break;
        }
        if (std::chrono::steady_clock::now() - beginning > std::chrono::milliseconds(timeout)) {
            result.timedOut = true;
            stop();
            break;
        }
        Sleep(5);
    }
    pump(sink, sink);
    DWORD code = 2;
    GetExitCodeProcess(process_, &code);
    result.code = int(code);
    return result;
}
} // namespace jade::native
