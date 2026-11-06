#include "BrokerProcess.hpp"
#include <chrono>

namespace jade::native {
std::wstring quoteWindowsArgument(const std::wstring &value) {
    return NativeProcess::quote(value);
}
BrokerProcess::BrokerProcess(const std::filesystem::path &executable,
                             const std::vector<std::wstring> &args, Output output, Output error) {
    process_.start(executable, args, {});
    pump_ = std::thread([this, output = std::move(output), error = std::move(error)] {
        try {
            while (process_.alive()) {
                process_.pump(output, error);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            process_.pump(output, error);
        } catch (...) {
            failure_ = std::current_exception();
            process_.stop();
        }
    });
}
BrokerProcess::~BrokerProcess() {
    stop();
    if (pump_.joinable())
        pump_.join();
}
void BrokerProcess::write(const std::string &text) {
    process_.write(text);
}
void BrokerProcess::end() {
    process_.endInput();
}
void BrokerProcess::stop() {
    process_.stop();
}
int BrokerProcess::wait() {
    if (pump_.joinable())
        pump_.join();
    if (failure_)
        std::rethrow_exception(failure_);
    return process_.exitCode();
}
} // namespace jade::native
