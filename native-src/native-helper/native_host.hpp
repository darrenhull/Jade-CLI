#pragma once
#include "helper.hpp"
#include <array>
#include <memory>
#include <functional>
#include <cstring>
#include <tlhelp32.h>
namespace jade {
using word=uintptr_t;
template<class T> T& at(word pointer,size_t offset=0){return *reinterpret_cast<T*>(pointer+offset);}
template<class Result,class... Args> Result invoke(word address,Args... args){return reinterpret_cast<Result(*)(Args...)>(address)(args...);}
inline word exported(HMODULE module,const char* name){auto value=GetProcAddress(module,name);if(!value)throw std::runtime_error(std::string("Native export missing: ")+name);return reinterpret_cast<word>(value);}
inline uint32_t native_utf8_length(const std::string& text){uint32_t bytes{},length{};auto status=invoke<int>(exported(GetModuleHandleW(L"jomos.dll"),"?intlStringUTF8VerifyCountchars@@YAHPEBEIAEAI1@Z"),text.data(),uint32_t(text.size()),&bytes,&length);if(status)throw std::runtime_error("Invalid native UTF-8 value: "+std::to_string(status));return length;}
inline std::string hex(const void* data,size_t count){static const char* digits="0123456789abcdef";std::string result;auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<count;++i){result+=digits[p[i]>>4];result+=digits[p[i]&15];}return result;}
inline std::array<unsigned char,22> oid_bytes(const std::string& oid){std::array<unsigned char,22> result{};if(oid.empty())return result;if(oid.size()!=44)throw std::runtime_error("Invalid native identity");for(size_t i=0;i<22;++i)result[i]=static_cast<unsigned char>(std::stoul(oid.substr(i*2,2),nullptr,16));return result;}
inline std::string identity(uint32_t kind,uint64_t instance){std::array<unsigned char,22> value{};std::memcpy(value.data(),&instance,8);std::memcpy(value.data()+8,&kind,4);return hex(value.data(),22);}
inline std::string oid_of(word object){return object?hex(reinterpret_cast<void*>(object+16),22):std::string{};}
inline std::string string_value(const json& row,const char* name){auto found=row.find(name);return found==row.end()||found->is_null()?std::string{}:found->get<std::string>();}
inline void put_oid(word object,const std::string& oid){auto bytes=oid_bytes(oid);std::memcpy(reinterpret_cast<void*>(object+16),bytes.data(),22);}
inline void put_text(word output,const std::string& value){auto text=wide(value);std::memcpy(reinterpret_cast<void*>(output),text.c_str(),(text.size()+1)*2);}
inline USHORT native_backtrace_from(CONTEXT current,ULONG skip,ULONG capacity,void** output){
    USHORT count{};ULONG walked{};
    while(current.Rip&&count<capacity&&walked<128){if(walked++>=skip)output[count++]=reinterpret_cast<void*>(current.Rip);DWORD64 image{};auto function=RtlLookupFunctionEntry(current.Rip,&image,nullptr);auto before=current.Rsp;
        if(function){void* data{};DWORD64 frame{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,current.Rip,function,&current,&data,&frame,nullptr);}
        else{current.Rip=at<DWORD64>(current.Rsp);current.Rsp+=8;}
        if(current.Rsp<=before)break;
    }return count;
}
inline USHORT native_backtrace(ULONG skip,ULONG capacity,void** output,DWORD*){CONTEXT current{};RtlCaptureContext(&current);return native_backtrace_from(current,skip,capacity,output);}
struct NativeHook {
    std::string name;word original{},thunk{};RUNTIME_FUNCTION unwind{};bool unwind_registered=false;
    std::function<word(const std::array<word,10>&)> handler;
};
class NativeHooks {
    struct Patch{word address,original;};
    std::vector<Patch> patches;
    struct CodePatch {word address;std::array<unsigned char,12> original;};
    std::vector<CodePatch> code_patches;
    std::vector<std::unique_ptr<NativeHook>> hooks;
    static word dispatch(NativeHook* hook,word a,word b,word c,word d,word e,word f,word g,word h,word i,word j) {
        try{return hook->handler({a,b,c,d,e,f,g,h,i,j});}
        catch(const std::exception& error){std::cerr<<"Native callback error ["<<hook->name<<"]: "<<error.what()<<"\n";return 1219;}
    }
public:
    void replace_entry(word address,word target){CodePatch patch{address,{}};std::memcpy(patch.original.data(),reinterpret_cast<void*>(address),12);code_patches.push_back(patch);
        std::array<unsigned char,12> code={0x48,0xb8};std::memcpy(code.data()+2,&target,8);code[10]=0xff;code[11]=0xe0;
        DWORD old{},unused{};if(!VirtualProtect(reinterpret_cast<void*>(address),12,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Cannot guard native entry");std::memcpy(reinterpret_cast<void*>(address),code.data(),12);VirtualProtect(reinterpret_cast<void*>(address),12,old,&unused);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),12);
    }
    void replace(word address,word value){DWORD previous{},unused{};if(!VirtualProtect(reinterpret_cast<void*>(address),8,PAGE_READWRITE,&previous))throw std::runtime_error("Cannot patch native import");patches.push_back({address,at<word>(address)});at<word>(address)=value;VirtualProtect(reinterpret_cast<void*>(address),8,previous,&unused);}
    word callback(std::string name,word original,std::function<word(const std::array<word,10>&)> handler){
        auto hook=std::make_unique<NativeHook>();hook->name=std::move(name);hook->original=original;hook->handler=std::move(handler);
        // Win64 bridge: insert the hook context before ten original integer/
        // pointer arguments. Shadow space and 16-byte stack alignment retained.
        std::vector<unsigned char> code={0x49,0x89,0xe3,0x48,0x83,0xec,0x68,0x4c,0x89,0x4c,0x24,0x20};
        for(unsigned char offset=0x28;offset<=0x50;offset+=8){code.insert(code.end(),{0x49,0x8b,0x43,offset,0x48,0x89,0x44,0x24,offset});}
        code.insert(code.end(),{0x4d,0x89,0xc1,0x49,0x89,0xd0,0x48,0x89,0xca,0x48,0xb9});
        auto append=[&](word value){auto p=reinterpret_cast<unsigned char*>(&value);code.insert(code.end(),p,p+8);};append(reinterpret_cast<word>(hook.get()));
        code.insert(code.end(),{0x48,0xb8});append(reinterpret_cast<word>(&dispatch));code.insert(code.end(),{0xff,0xd0,0x90,0x48,0x83,0xc4,0x68,0xc3});
        auto unwind_offset=(code.size()+3)&~size_t(3),total=unwind_offset+8;
        auto memory=VirtualAlloc(nullptr,total,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!memory)throw std::runtime_error("Cannot allocate native callback");
        std::memcpy(memory,code.data(),code.size());
        // Seven-byte prolog, one UWOP_ALLOC_SMALL for its 104-byte stack frame.
        const unsigned char unwind_info[]={1,7,1,0,7,0xc2,0,0};std::memcpy(static_cast<unsigned char*>(memory)+unwind_offset,unwind_info,8);
        hook->unwind={0,DWORD(code.size()),DWORD(unwind_offset)};
        if(!RtlAddFunctionTable(&hook->unwind,1,reinterpret_cast<DWORD64>(memory))){VirtualFree(memory,0,MEM_RELEASE);throw std::runtime_error("Cannot register native callback unwind metadata");}hook->unwind_registered=true;
        DWORD previous{};VirtualProtect(memory,total,PAGE_EXECUTE_READ,&previous);FlushInstructionCache(GetCurrentProcess(),memory,code.size());
        hook->thunk=reinterpret_cast<word>(memory);auto result=hook->thunk;hooks.push_back(std::move(hook));return result;
    }
    ~NativeHooks(){for(auto p=code_patches.rbegin();p!=code_patches.rend();++p){DWORD old{},unused{};VirtualProtect(reinterpret_cast<void*>(p->address),12,PAGE_EXECUTE_READWRITE,&old);std::memcpy(reinterpret_cast<void*>(p->address),p->original.data(),12);VirtualProtect(reinterpret_cast<void*>(p->address),12,old,&unused);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p->address),12);}for(auto p=patches.rbegin();p!=patches.rend();++p){DWORD old{},unused{};VirtualProtect(reinterpret_cast<void*>(p->address),8,PAGE_READWRITE,&old);at<word>(p->address)=p->original;VirtualProtect(reinterpret_cast<void*>(p->address),8,old,&unused);}for(auto& hook:hooks){if(hook->unwind_registered)RtlDeleteFunctionTable(&hook->unwind);VirtualFree(reinterpret_cast<void*>(hook->thunk),0,MEM_RELEASE);}}
};
struct Import {std::string library,name;word slot,original;};
inline std::vector<Import> imports(HMODULE module){
    std::vector<Import> result;auto base=reinterpret_cast<word>(module);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;if(!rva)return result;
    auto table=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+rva);
    for(;table->Name;++table){auto library=std::string(reinterpret_cast<char*>(base+table->Name));
        auto names=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+(table->OriginalFirstThunk?table->OriginalFirstThunk:table->FirstThunk));auto slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+table->FirstThunk);
        for(size_t i=0;names[i].u1.AddressOfData;++i){if(IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal))continue;auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names[i].u1.AddressOfData);result.push_back({library,reinterpret_cast<char*>(entry->Name),reinterpret_cast<word>(&slots[i].u1.Function),slots[i].u1.Function});}}
    return result;
}
inline std::vector<std::pair<std::string,word>> exports(HMODULE module){
    std::vector<std::pair<std::string,word>> result;auto base=reinterpret_cast<word>(module);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto directory=reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base+nt->OptionalHeader.DataDirectory[0].VirtualAddress);auto names=reinterpret_cast<DWORD*>(base+directory->AddressOfNames);auto ordinals=reinterpret_cast<WORD*>(base+directory->AddressOfNameOrdinals);auto addresses=reinterpret_cast<DWORD*>(base+directory->AddressOfFunctions);
    for(size_t i=0;i<directory->NumberOfNames;++i)result.emplace_back(reinterpret_cast<char*>(base+names[i]),base+addresses[ordinals[i]]);return result;
}
class AccessGuard {
    NativeHooks hooks;
    bool tracing=false;
    static const std::set<std::string>& denied(){static const std::set<std::string> names={"CreateFileW","CreateFileA","CreateFile2","NtCreateFile","NtOpenFile","socket","WSASocketW","WSASocketA","connect","WSAConnect","LoadLibraryW","LoadLibraryA","LoadLibraryExW","LoadLibraryExA"};return names;}
    std::map<std::pair<std::string,word>,word> guarded;
    word protect_api(const std::string& name,word original,const fs::path& install){
        auto key=std::make_pair(name,original);if(guarded.contains(key))return guarded.at(key);
        auto callback=hooks.callback(name,original,[this,name,original,install](const std::array<word,10>& args)->word{
            if(name=="LoadLibraryW"||name=="LoadLibraryExW"){HMODULE loaded{};if(GetModuleHandleExW(0,reinterpret_cast<LPCWSTR>(args[0]),&loaded))return reinterpret_cast<word>(loaded);}
            if(name=="CreateFileW"&&args[0]){fs::path file(reinterpret_cast<wchar_t*>(args[0]));auto leaf=filename(file.filename());auto cleaned=filename(file);if(cleaned.starts_with("\\\\?\\"))file=filepath(cleaned.substr(4));
                if(jade::canonical(file.parent_path())==jade::canonical(install/L"logs")&&leaf.starts_with("jommsg")&&file.extension()==L".log")return invoke<word>(original,args[0],args[1],args[2],args[3],args[4],args[5],args[6]);}
            json attempt={{"operation",name}};if((name=="LoadLibraryW"||name=="LoadLibraryExW"||name=="CreateFileW")&&args[0])attempt["target"]=utf8(reinterpret_cast<wchar_t*>(args[0]));
            if(tracing){void* frames[64]{};auto count=native_backtrace(0,64,frames,nullptr);attempt["frames"]=json::array();for(USHORT i=0;i<count;i++){HMODULE owner{};wchar_t module_name[MAX_PATH]{};if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[i]),&owner)){GetModuleFileNameW(owner,module_name,MAX_PATH);attempt["frames"].push_back({{"module",filename(fs::path(module_name).filename())},{"rva",reinterpret_cast<word>(frames[i])-reinterpret_cast<word>(owner)}});}else attempt["frames"].push_back({{"address",reinterpret_cast<word>(frames[i])}});}}attempts.push_back(attempt);SetLastError(ERROR_ACCESS_DENIED);
            if(name.starts_with("Nt"))return 0xc0000022;if(name.starts_with("LoadLibrary"))return 0;return word(-1);
        });guarded[key]=callback;return callback;
    }
public:
    json attempts=json::array();size_t entries{};
    explicit AccessGuard(const fs::path& install,bool trace=false):tracing(trace){
        auto jom=GetModuleHandleW(L"jom.dll");
        for(auto name:{"jomInitialize","jomSignOn"}){auto original=exported(jom,name);auto callback=hooks.callback(name,original,[this,name](const std::array<word,10>&)->word{attempts.push_back({{"operation",name}});return 1219;});hooks.replace_entry(original,callback);++entries;}
        auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot enumerate native modules");
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(Module32FirstW(snapshot,&entry))do{if(!within(install,fs::path(entry.szExePath)))continue;
            for(const auto& item:imports(entry.hModule)){
                if(item.name=="GetProcAddress"){
                    auto callback=hooks.callback(item.name,item.original,[this,item,install](const std::array<word,10>& args)->word{
                        auto original=invoke<word>(item.original,args[0],args[1]);if(!original||args[1]<=0xffff)return original;
                        std::string name(reinterpret_cast<char*>(args[1]));return denied().contains(name)?protect_api(name,original,install):original;
                    });hooks.replace(item.slot,callback);++entries;
                }else if(denied().contains(item.name)){hooks.replace(item.slot,protect_api(item.name,item.original,install));++entries;}
            }
        }while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);
    }
};
}
