#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
namespace jade::native {
struct ProcessResult {
    int code = 2;
    bool cancelled = false, timedOut = false;
    std::string output;
    bool success() const {
        return code == 0 && !cancelled && !timedOut;
    }
};
class NativeProcess {
    HANDLE process_ = nullptr, input_ = nullptr, out_ = nullptr, err_ = nullptr, job_ = nullptr;
    std::string lines_, outPending_, errPending_;
    std::mutex inputMutex_;

  public:
    using Output = std::function<void(const std::string &)>;
    ~NativeProcess();
    NativeProcess() = default;
    NativeProcess(const NativeProcess &) = delete;
    NativeProcess &operator=(const NativeProcess &) = delete;
    static std::wstring quote(const std::wstring &value);
    void start(const std::filesystem::path &exe, const std::vector<std::wstring> &args,
               const std::filesystem::path &cwd);
    void write(const std::string &text);
    void endInput();
    int exitCode() const;
    unsigned long processId() const;
    void stop();
    bool alive() const;
    bool waitExit(unsigned milliseconds);
    void pump(const Output &stdoutOutput, const Output &stderrOutput);
    std::string line(std::atomic_bool *cancelled, unsigned timeout, const Output &output);
    ProcessResult run(const std::filesystem::path &exe, const std::vector<std::wstring> &args,
                      const std::filesystem::path &cwd, std::atomic_bool *cancelled,
                      unsigned timeout, const Output &output);
};
std::string utf8(const std::wstring &value);
std::wstring wide(const std::string &value);
} // namespace jade::native
