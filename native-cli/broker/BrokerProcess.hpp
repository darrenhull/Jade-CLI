#pragma once
#include "../engine/NativeProcess.hpp"
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace jade::native {
class BrokerProcess {
    NativeProcess process_;
    std::thread pump_;
    std::exception_ptr failure_;

  public:
    using Output = std::function<void(const std::string &)>;
    BrokerProcess(const std::filesystem::path &, const std::vector<std::wstring> &, Output, Output);
    BrokerProcess(const BrokerProcess &) = delete;
    ~BrokerProcess();
    void write(const std::string &);
    void end();
    int wait();
    void stop();
};
std::wstring quoteWindowsArgument(const std::wstring &);
} // namespace jade::native
