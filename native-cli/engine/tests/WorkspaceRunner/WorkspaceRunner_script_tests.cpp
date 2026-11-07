#include "WorkspaceRunner.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    json request = {{"sourceText", "vars\n value : Integer;\nbegin\n write 'Cow';\nend;"},
                    {"source", "untitled:Jade Workspace"},
                    {"config", {{"schema", "SJetSchema"}}}};
    WorkspaceRunner::validate(request);
    const auto launcher = WorkspaceRunner::launcher(request["sourceText"], "nonce");
    assert(launcher.find("write ' & \"'\" & 'Cow' & \"'\" & ';") != std::string::npos);
    assert(launcher.find("true, null, err, pos, len, self") != std::string::npos);
    auto rejects = [&](json changed) {
        try {
            WorkspaceRunner::validate(changed);
        } catch (const std::exception &) {
            return true;
        }
        return false;
    };
    auto invalid = request;
    invalid["sourceText"] = std::string("begin\0end;", 10);
    assert(rejects(invalid));
    invalid["sourceText"] = std::string(65537, 'x');
    assert(rejects(invalid));
    invalid = request;
    invalid["config"]["schema"] = "RootSchema";
    assert(rejects(invalid));
    invalid["config"]["schema"] = "Bad\nschema";
    assert(rejects(invalid));
    invalid = request;
    invalid["lineOffset"] = -1;
    assert(rejects(invalid));

    ProcessResult process;
    process.code = 0;
    process.output = "Opening database "
                     "...\r\nnonce:BEGIN\r\r\nCow\r\r\nnonce:RESULT:0:0:0\r\r\nDatabase closed\r\n";
    auto result = WorkspaceRunner::result(process, "nonce", request, "working/system", 42);
    assert(result["passed"] == true);
    assert(result["report"]["compiled"] == true && result["report"]["executed"] == true);
    assert(result["report"]["output"] == "Cow\n");
    assert(result["report"]["elapsedMilliseconds"] == 42);
    assert(result["report"]["diagnostics"].empty());
    process.output = "nonce:BEGIN\nnonce:RESULT:6027:12:7\n";
    result = WorkspaceRunner::result(process, "nonce", request, "working/system", 0);
    assert(result["passed"] == false && result["code"] == 6027);
    assert(result["report"]["compiled"] == false);
    const auto diagnostic = result["report"]["diagnostics"][0];
    assert(diagnostic["position"] == 12 && diagnostic["length"] == 7);
    assert(diagnostic["source"] == "untitled:Jade Workspace");
    process.output = "wrongnonce:RESULT:0:0:0\n";
    assert(!WorkspaceRunner::result(process, "nonce", request, "working/system", 0)["passed"]
                .get<bool>());
    process.output = "nonce:BEGIN\nnonce:RESULT:0:0:0\n";
    process.cancelled = true;
    result = WorkspaceRunner::result(process, "nonce", request, "working/system", 0);
    assert(result["cancelled"] == true && result["passed"] == false);
    process.cancelled = false;
    process.timedOut = true;
    assert(WorkspaceRunner::result(process, "nonce", request, "working/system", 0)["timedOut"] ==
           true);
    process.timedOut = false;
    process.output = "nonce:BEGIN\n" + std::string(256 * 1024 - 1, 'x') + "é\nnonce:RESULT:0:0:0\n";
    result = WorkspaceRunner::result(process, "nonce", request, "working/system", 0);
    assert(result["report"]["outputTruncated"] == true);
    assert(result["report"]["output"].get<std::string>().size() == 256 * 1024 - 1);
    result.dump(); // Truncation must leave valid UTF-8 for JSON serialization.
    process.output = std::string(33000, 'x') + "é" + std::string(32767, 'x');
    WorkspaceRunner::result(process, "nonce", request, "working/system", 0).dump();
    std::cout << "WorkspaceRunner native launcher, input limits, void result, diagnostic, "
                 "cancellation contracts passed\n";
}
