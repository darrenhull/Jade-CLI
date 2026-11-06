#include "WorkspaceRunner.hpp"
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>
using namespace jade::native;
int main() {
    const auto *seedValue = std::getenv("JADE_CPP_WORKSPACE_TEST_SEED");
    if (!seedValue) { std::cout << "SKIP: set JADE_CPP_WORKSPACE_TEST_SEED to a private disposable TEMP seed\n"; return 0; }
    const auto seed = std::filesystem::path(wide(seedValue));
    if (!contains(std::filesystem::temp_directory_path(), seed) ||
        !std::filesystem::is_regular_file(seed / "_control.dat"))
        throw std::runtime_error("Workspace proof requires an existing private TEMP database seed.");
    const auto root = std::filesystem::temp_directory_path() / ("jade-workspace-proof-" + uniqueId());
    const auto src = root / "src"; std::filesystem::create_directories(src);
    const auto header = std::string("jadeVersionNumber \"25.0.02\";\nschemaDefinition\nSJetSchema subschemaOf RootSchema partialDefinition;\n");
    const auto domain = src / "WorkspaceLocalValue.cls";
    auto domainSource = [&](int value) { return header + "typeHeaders\nWorkspaceLocalValue subclassOf Object transient;\ntypeDefinitions\nWorkspaceLocalValue completeDefinition\n(\njadeMethodDefinitions\nvalue():Integer;\n)\ntypeSources\nWorkspaceLocalValue (\njadeMethodSources\nvalue\n{\nvalue():Integer;\nbegin\n return " + std::to_string(value) + ";\nend;\n}\n)\n"; };
    writeText(src / "SJetSchema.scm", header); writeText(domain, domainSource(42));
    json config = {{"installPath", "C:/Jade2025"}, {"databasePath", utf8(seed.wstring())},
        {"iniPath", utf8((seed.parent_path() / "working.ini").wstring())}, {"server", "singleUser"},
        {"schema", "SJetSchema"}, {"workspaceRoots", {utf8(src.wstring())}},
        {"storagePath", utf8((root / "private").wstring())}, {"timeoutMilliseconds", 60000}};
    WorkingEngine working(std::filesystem::current_path()); WorkspaceRunner runner(working);
    auto invoke = [&](const std::string &text, std::atomic_bool *cancelled = nullptr) {
        const auto start = std::chrono::steady_clock::now();
        auto result = runner.execute({{"config", config}, {"sourceText", text}, {"source", "untitled:Native Workspace proof"}},
                                    [](const auto &line) { std::cerr << line; }, cancelled);
        result["proofWallMilliseconds"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return result;
    };
    auto cold = invoke("write 42;"); assert(cold.value("passed", false));
    auto warm = invoke("write 42;"); assert(warm.value("passed", false));
    const auto localScript = "vars\n value : WorkspaceLocalValue;\nbegin\n create value transient;\n write value.value();\nepilog\n delete value;\nend;";
    auto first = invoke(localScript); assert(first.value("passed", false)); assert(first["report"]["output"] == "42\n");
    writeText(domain, domainSource(43));
    auto updated = invoke(localScript); assert(updated.value("passed", false)); assert(updated["report"]["output"] == "43\n");
    auto quoted = invoke("begin write \"Cow's milk\"; write 'Grüße 🐄'; end;");
    assert(quoted.value("passed", false)); quoted.dump();
    auto compiler = invoke("// 🐄\r\nbegin\r\n write missingWorkspaceValue;\r\nend;");
    assert(!compiler.value("passed", true)); assert(compiler["code"] == 6027);
    assert(compiler["report"]["diagnostics"][0]["position"] == wide("// 🐄\r\nbegin\r\n write ").size());
    assert(compiler["report"]["diagnostics"][0]["message"] != "Jade compiler error 6027");
    auto selection = invoke("write 42;");
    assert(selection.value("passed", false) && selection["report"]["output"] == "42\n");
    std::cout << "STATEMENT_SELECTION_NATIVE_RESULT=" << selection.dump() << '\n';
    auto runtime = invoke("vars\n zero : Integer;\nbegin\n write 10 div zero;\nend;");
    assert(!runtime.value("passed", true)); assert(runtime["code"] == 4030);
    assert(runtime["report"]["diagnostics"][0]["message"] == "Division by zero");
    auto read = invoke("vars\n text : String;\nbegin\n read text;\n write text;\nend;");
    assert(!read.value("timedOut", false));
    config["timeoutMilliseconds"] = 6000;
    auto timeout = invoke("begin process.sleep(30000); end;");
    assert(timeout.value("timedOut", false) && !timeout.value("passed", true));
    config["timeoutMilliseconds"] = 60000;
    std::atomic_bool cancelled = false;
    std::thread stop([&] { Sleep(6000); cancelled.store(true); });
    auto aborted = invoke("begin process.sleep(30000); end;", &cancelled); stop.join();
    assert(aborted.value("cancelled", false) && !aborted.value("passed", true));
    auto recovered = invoke("begin write 'RECOVERED'; end;");
    assert(recovered.value("passed", false) && recovered["report"]["output"] == "RECOVERED\n");
    working.disconnect();
    writeText(root / "proof.json", json{{"coldWrite42", cold}, {"warmWrite42", warm}, {"first", first}, {"updated", updated}, {"quoted", quoted},
        {"compiler", compiler}, {"selection", selection}, {"runtime", runtime}, {"read", read}, {"timeout", timeout},
        {"cancelled", aborted}, {"recovered", recovered}}.dump(2));
    std::cout << "Workspace native local overlays, quotes, Unicode positions, errors, timeout, cancellation and recovery passed: " << utf8(root.wstring()) << '\n';
}
