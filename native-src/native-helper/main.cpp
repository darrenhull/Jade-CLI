#include "helper.hpp"
int main(){
    using namespace jade;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception)->LONG{HMODULE module{};wchar_t name[MAX_PATH]{};auto address=exception->ExceptionRecord->ExceptionAddress;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module);GetModuleFileNameW(module,name,MAX_PATH);std::fwprintf(stderr,L"Native worker fault 0x%08lx in %ls +0x%llx\n",exception->ExceptionRecord->ExceptionCode,name,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(module)));return EXCEPTION_EXECUTE_HANDLER;});
    json input=json::object(),result;int code=0;
    try{
        std::string line;if(!std::getline(std::cin,line)||line.size()>1024*1024)throw std::runtime_error("Missing or oversized request");
        input=json::parse(line);
        if(input.value("protocolVersion",0)!=1)throw std::runtime_error("Unsupported protocol version");
        Context context(input);auto operation=input.at("operation").get<std::string>();
        if(operation=="cache.import")result=import_cache(context);
        else if(operation=="cache.refresh")result=refresh_system_cache(context);
        else if(operation=="cache.inspect"){auto cache=system_cache(context);result={{"success",true},{"typeCount",cache["types"].size()},{"fingerprint",context.fingerprint}};}
        else if(operation=="index.refresh"){result=refresh_index(context);result["entryCount"]=result["entries"].size();if(!input.value("includeEntries",true))result.erase("entries");}
        else if(operation=="index.resolve")result=resolve_type(context,input.at("schema"),input.at("name"));
        else if(operation=="value.selfTest")result=native_value_test(context);
        else if(operation=="guard.selfTest")result=native_access_test(context);
        else if(operation=="compile"||operation=="execute"||operation=="test.run"){result=native_operation(context);if(!result.value("success",false))code=result.value("failureKind",std::string{})=="metadata"?2:1;}
        else throw std::runtime_error("Unknown operation: "+operation);
    }catch(const std::exception& error){code=2;result={{"success",false},{"error",error.what()}};}
    result["protocolVersion"]=1;result["id"]=input.value("id",std::string{});result["stage"]="result";
    std::cout<<result.dump()<<std::endl;return code;
}
