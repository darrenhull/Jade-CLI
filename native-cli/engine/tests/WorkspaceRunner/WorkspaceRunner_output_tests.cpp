#include "WorkspaceRunner.hpp"
#include <chrono>
#include <iostream>
using namespace jade::native;
int main() {
    ProcessResult process;
    process.code = 0;
    const std::string payload(200000, '\r');
    process.output = "nonce:BEGIN\n" + payload + "X\nnonce:RESULT:0:0:0\n";
    const json request = {{"sourceText", "begin end;"}};
    const auto started = std::chrono::steady_clock::now();
    const auto result = WorkspaceRunner::result(process, "nonce", request, "working", 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "Workspace 200KB output normalization elapsed: " << elapsed << "ms\n";
    if (!result.value("passed", false) || result["report"]["output"] != payload + "X\n")
        throw std::runtime_error("Standalone carriage returns must be preserved.");
    if (elapsed > 1000)
        throw std::runtime_error("Bounded Workspace output normalization must remain linear; " +
                                 std::to_string(elapsed) + "ms for 200KB.");
    std::cout << "Workspace 200KB carriage-return output preserved and normalized in " << elapsed << "ms\n";
}
