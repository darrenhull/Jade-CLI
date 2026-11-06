#pragma once
#include "native_host.hpp"
#include <jomparam.hpp>
#include <atomic>
namespace jade {
// Immutable owned value. Copies share stable storage; capture performs the
// native deep copy before a borrowed argument or interpreter node can change.
class NativeValue {
    inline static std::atomic<size_t> live{}, captured{};
    struct State {
        State(){++live;}
        std::array<word,5> storage{};
        std::unique_ptr<std::array<word,66>> list_storage;
        std::vector<NativeValue> children;
        word address() const {return reinterpret_cast<word>(list_storage?list_storage->data():storage.data());}
        ~State(){auto module=GetModuleHandleW(L"jomutil.dll");if(module)invoke<int>(exported(module,"paramDeleteJadeAny"),address());--live;}
    };
    std::shared_ptr<State> state;
    explicit NativeValue(std::shared_ptr<State> value):state(std::move(value)){}
public:
    static size_t live_count(){return live.load();}
    static size_t capture_count(){return captured.load();}
    NativeValue()=default;
    NativeValue(int32_t value):NativeValue(scalar(DSKINTEGER,value)){}
    NativeValue(bool value):NativeValue(scalar(DSKBOOLEAN,uint8_t(value))){}
    template<class T>static NativeValue scalar(PFormat format,T value){auto result=std::make_shared<State>();at<uint16_t>(result->address())=format;at<T>(result->address(),8)=value;return NativeValue(std::move(result));}
    static NativeValue capture(word source){++captured;auto result=std::make_shared<State>();auto address=result->address();auto format=at<uint16_t>(source);
        if(format==DSKPARAMLIST){result->list_storage=std::make_unique<std::array<word,66>>();address=result->address();at<uint16_t>(address)=format;auto count=at<uint16_t>(source,8);if(count>64)throw std::runtime_error("Native parameter list exceeds SDK capacity");result->children.reserve(count);for(uint16_t i=0;i<count;i++)result->children.push_back(capture(at<word>(source,16+i*8)));at<uint16_t>(address,8)=count;for(uint16_t i=0;i<count;i++)at<word>(address,16+i*8)=result->children[i].address();}
        else if(format==DSKUNDEF||format==DSKNULL)at<uint16_t>(address)=format;
        else {auto status=invoke<int>(exported(GetModuleHandleW(L"jomutil.dll"),"?paramcpy@@YAHPEAUShortDskParam@@PEBU1@@Z"),address,source);if(status)throw std::runtime_error("Native value capture failed: "+std::to_string(status)+" format="+std::to_string(format));}
        return NativeValue(std::move(result));
    }
    static NativeValue zero(uint16_t format){std::array<word,5> input{};auto address=reinterpret_cast<word>(input.data());at<uint16_t>(address)=format;static wchar_t empty[] = L"";if(format==DSKCSTRING)at<word>(address,8)=reinterpret_cast<word>(empty);return capture(address);}
    word address()const {if(!state)throw std::runtime_error("Missing native value");return state->address();}
    explicit operator bool()const{return bool(state);}
    bool is_integer()const{return format()==DSKINTEGER||format()==DSKINTEGER64;}
    bool is_reference()const{return format()==DSKOBJECTID||format()==DSKSHORTOBJECTID||format()==DSKBUFFERPTR;}
    std::string reference()const{if(format()==DSKOBJECTID)return hex(reinterpret_cast<void*>(address()+8),22);if(format()==DSKSHORTOBJECTID)return hex(reinterpret_cast<void*>(address()+8),12)+std::string(20,'0');if(format()==DSKBUFFERPTR){auto buffer=at<word>(address(),8);return buffer?hex(reinterpret_cast<void*>(buffer+50),22):std::string(44,'0');}throw std::runtime_error("Expected native object reference");}
    bool is_string()const{return format()==DSKCSTRING||format()==DSKCHARBLOCKPOINTER||format()==DSKJADESTRING||format()==48;}
    std::wstring string()const{Character* text{};Size length{};auto status=paramGetString(*reinterpret_cast<const ShortDskParam*>(address()),text,&length);if(status)throw std::runtime_error("Expected native String");return std::wstring(reinterpret_cast<wchar_t*>(text),length);}
    bool operator==(const NativeValue& other)const{return equal(other);}
    uint16_t format()const{return at<uint16_t>(address());}
    const std::vector<NativeValue>& elements()const{if(format()!=DSKPARAMLIST)throw std::runtime_error("Expected native parameter list");return state->children;}
    void copy_to(word target)const{if(format()==DSKUNDEF||format()==DSKNULL){at<uint16_t>(target)=format();return;}auto status=invoke<int>(exported(GetModuleHandleW(L"jomutil.dll"),"?paramcpy@@YAHPEAUShortDskParam@@PEBU1@@Z"),target,address());if(status)throw std::runtime_error("Native value assignment failed: "+std::to_string(status));}
    bool equal(const NativeValue& other)const{bool result{};auto status=invoke<int>(exported(GetModuleHandleW(L"jomutil.dll"),"?paramsAreEqual@@YAHPEBUDskParam@@0PEA_N@Z"),address(),other.address(),&result);if(status)throw std::runtime_error("Native value comparison failed: "+std::to_string(status));return result;}
    int64_t integer()const{if(format()==DSKINTEGER)return at<int32_t>(address(),8);if(format()==DSKINTEGER64)return at<int64_t>(address(),8);throw std::runtime_error("Expected native Integer or Integer64");}
    bool boolean()const{if(format()!=DSKBOOLEAN)throw std::runtime_error("Expected native Boolean");return at<uint8_t>(address(),8)!=0;}
};
}
