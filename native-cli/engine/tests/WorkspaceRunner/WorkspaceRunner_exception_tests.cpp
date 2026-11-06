#include "WorkspaceRunner.hpp"
#include <cassert>
#include <iostream>
using namespace jade::native;
int main() {
    json request={{"sourceText","vars\r\n zero : Integer;\r\nbegin\r\n write 10 div zero;\r\nend;"},
                  {"source","vscode-notebook-cell:private"},{"lineOffset",3},{"columnOffset",5}};
    ProcessResult process;process.code=1205;
    process.output="nonce:BEGIN\r\r\nBefore\r\r\nScript terminated with exception 1205\r\nDatabase closed\r\n";
    const std::string log="Unhandled NormalException on ...\r\n NormalException: 4030 (not continuable, resumable)\r\n* Division by zero\r\n Caused By:\r\n Method: JadeScript::workspace(55)  --  write 10 div zero;\r\n";
    const auto result=WorkspaceRunner::result(process,"nonce",request,"working/system",12,log);
    assert(result["passed"]==false && result["code"]==4030);
    assert(result["report"]["compiled"]==true && result["report"]["executed"]==true);
    assert(result["report"]["output"]=="Before\n");
    assert(result["report"]["diagnostics"][0]["line"]==7);
    assert(result["report"]["diagnostics"][0]["message"]=="Division by zero");
    assert(result["report"]["error"]=="Division by zero");
    auto ordinary=process;ordinary.code=0;ordinary.output="nonce:BEGIN\nA\rB\r\r\nnonce:RESULT:0:0:0\n";
    assert(WorkspaceRunner::result(ordinary,"nonce",request,"working/system",0)["report"]["output"]=="A\rB\n");
    std::cout<<"Workspace native exception log identity, source mapping and output newline contracts passed\n";
}
