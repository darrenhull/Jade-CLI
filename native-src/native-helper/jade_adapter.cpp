#include "native_value.hpp"

#include <algorithm>
#include <jomparam.hpp>
namespace jade {
class NativeProgram {
    Context& context;
    HMODULE jom{},compiler{},interpreter{},messages{},primitive_library{};
    word comp{},interp{};
    std::vector<std::unique_ptr<unsigned char[]>> arena;
    std::map<std::string,json> types,methods,usages,properties,schemas,constants,keys;
    std::map<std::string,word> memory_classes,memory_schemas,roots;
    std::map<std::string,std::map<std::string,std::string>> visible;
    std::set<std::string> needed;
    std::map<std::string,std::string> native_features;
    std::map<std::string,std::vector<std::string>> metadata_collections;
    std::map<std::string,std::set<std::string>> constant_usages;
    std::string primary,selected;
    json index,diagnostics=json::array(),errors=json::array();
    NativeHooks metadata_hooks;
    std::unique_ptr<AccessGuard> access;
    NativeHooks service_hooks;
    std::map<std::string,word> objects;
    std::map<word,std::vector<word>> self_references;
    std::map<word,std::string> buffer_ids;
    std::map<std::pair<std::string,std::string>,NativeValue> values;
    std::map<std::string,std::vector<NativeValue>> collection_values;
    std::map<std::string,std::vector<std::vector<NativeValue>>> dictionary_keys;
    struct LocalIterator {std::string collection;int64_t position=-1;bool started=false;};std::map<word,LocalIterator> local_iterators;word iterator_vtable{};
    word iterator_service(const std::string& full,const std::array<word,10>& a){
        auto end=full.find('@');auto name=full.substr(1,end-1);if(context.request.value("traceNative",false))std::cerr<<"Iterator operation "<<name<<"\n";
        if(name=="createInstance"){
            auto collection=hex(reinterpret_cast<void*>(a[0]),22);if(!objects.contains(collection))throw std::runtime_error("Iterator collection is not local");
            if(!iterator_vtable){auto original=exported(jom,"??_7Iterator@@6B@");iterator_vtable=allocate(1024);std::map<word,std::string> names;for(const auto& [entry,address]:exports(jom))if(entry.find("@Iterator@@")!=std::string::npos)names[address]=entry;
                for(size_t slot=0;slot<1024;slot+=8){auto target=at<word>(original,slot);MEMORY_BASIC_INFORMATION page{};if(!VirtualQuery(reinterpret_cast<void*>(target),&page,sizeof(page))||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))break;auto entry=names.contains(target)?names.at(target):std::string("?unsupported@Iterator@@");at<word>(iterator_vtable,slot)=service_hooks.callback(entry,target,[this,entry](const std::array<word,10>& args)->word{try{return iterator_service(entry,args);}catch(const std::exception& error){service_errors.push_back({{"operation",entry},{"error",error.what()}});abort_roots();return 0;}});}}
            auto object=allocate(152);at<word>(object)=iterator_vtable;local_iterators[object]={collection,-1,false};return object;
        }
        if(!local_iterators.contains(a[0]))throw std::runtime_error("Native iterator is not local");auto& iterator=local_iterators.at(a[0]);if(!objects.contains(iterator.collection))throw std::runtime_error("Iterator collection was deleted");auto& items=collection_values[iterator.collection];
        if(name=="deleteSelf"){local_iterators.erase(a[0]);return 0;}
        if(name=="reset"){iterator.position=-1;iterator.started=false;return 0;}
        if(name=="isValid")return 1;
        if(name=="getCollection"){auto oid=oid_bytes(iterator.collection);std::memcpy(reinterpret_cast<void*>(a[1]),oid.data(),22);return 0;}
        if(name=="next"||name=="next_"||name=="back"||name=="back_"||name=="current"){
            if(!iterator.started){iterator.position=name=="back"||name=="back_"?int64_t(items.size()):-1;iterator.started=true;}
            if(name=="next"||name=="next_")++iterator.position;else if(name=="back"||name=="back_")--iterator.position;
            bool valid=iterator.position>=0&&size_t(iterator.position)<items.size();if(valid)items[size_t(iterator.position)].copy_to(a[1]);if(name.ends_with('_')){at<bool>(a[2])=valid;return 0;}return valid;
        }
        throw std::runtime_error("Unsupported local iterator operation: "+full);
    }
    std::map<std::string,std::string> templates;
    std::vector<std::pair<std::string,std::string>> frames;
    std::vector<word> active_roots;
    json assertions=json::array(),failures=json::array(),service_errors=json::array();
    std::vector<NativeValue> expected_exceptions;
    struct ExceptionHandler {std::string receiver,target,method;word root{},callback{};bool global=false,active=true;uint64_t token{};};
    std::vector<ExceptionHandler> exception_handlers;uint64_t next_handler_token{};
    void disarm_root(word root){std::erase_if(exception_handlers,[&](const auto& handler){return !handler.global&&handler.root==root;});}
    size_t matched_exceptions=0;
    uint64_t next_instance=0x6ffff100;
    word cache_manager{};
    struct ResumeStatement {word root;};
    void progress(const std::string& stage,const std::string& message){if(context.request.value("events",false))std::cout<<json{{"protocolVersion",1},{"id",context.request["id"]},{"stage",stage},{"message",message}}.dump()<<std::endl;}
    std::array<unsigned char,40> snapshot(word node){std::array<unsigned char,40> value{};auto table=at<word>(node);auto format=invoke<uint8_t>(at<word>(table,0x20),node);invoke<int>(at<word>(table,0xa0),node,format,uint8_t(0),value.data());return value;}
    json read_value(word parameter){auto tag=at<uint16_t>(parameter);if(tag==4||tag==16)return at<int32_t>(parameter,8);if(tag==7)return bool(at<uint8_t>(parameter,8));if(tag==0||tag==36)return nullptr;
        if(tag==30){json result=json::array();auto count=at<uint16_t>(parameter,8);for(uint16_t i=0;i<count;i++)result.push_back(read_value(at<word>(parameter,16+i*8)));return result;}
        if(tag==DSKBINARY||tag==DSKBINARYBLOCKPOINTER||tag==DSKJADEBINARY){BYTE* data{};Size length{};auto status=paramGetBinary(*reinterpret_cast<const ShortDskParam*>(parameter),data,length);if(status)throw std::runtime_error("Native binary read failed: "+std::to_string(status));return json{{"binary",hex(data,length)}};}
        if(tag==DSKSTRINGUTF8||tag==DSKJADESTRINGUTF8){StringUtf8Info text{};auto status=paramGetStringUtf8Ref(*reinterpret_cast<const ShortDskParam*>(parameter),text);if(status)throw std::runtime_error("Native UTF-8 read failed: "+std::to_string(status));return json{{"utf8",std::string(reinterpret_cast<char*>(text.utf8BytePtr),text.size)},{"length",text.length}};}
        if(tag==6||tag==8||tag==17||tag==18||tag==19||tag==20||tag==21||tag==42||tag==52||tag==53||tag==57)return json{{"nativeFormat",tag},{"bytes",hex(reinterpret_cast<void*>(parameter+8),32)}};
        if(tag==DSKCSTRING||tag==48||tag==DSKCHARBLOCKPOINTER||tag==DSKJADESTRING){Character* text{};Size length{};auto status=paramGetString(*reinterpret_cast<const ShortDskParam*>(parameter),text,&length);if(status)throw std::runtime_error("Native String read failed: "+std::to_string(status));return utf8(std::wstring(reinterpret_cast<wchar_t*>(text),length));}
        if(tag==3)return json{{"oid",hex(reinterpret_cast<void*>(parameter+8),22)}};
        if(tag==24)return json{{"oid",hex(reinterpret_cast<void*>(parameter+8),12)+std::string(20,'0')}};
        if(tag==26){auto buffer=at<word>(parameter,8);return json{{"oid",buffer?hex(reinterpret_cast<void*>(buffer+50),22):std::string(44,'0')}};}
        throw std::runtime_error("Unsupported native value format: "+std::to_string(tag));
    }
    void write_value(word parameter,const json& value,uint16_t tag=0){
        if(value.is_null()){at<uint16_t>(parameter)=DSKNULL;return;}
        if(value.is_object()&&value.contains("nativeFormat")){auto bytes=value.at("bytes").get<std::string>();if(bytes.size()!=64)throw std::runtime_error("Invalid fixed primitive body");at<uint16_t>(parameter)=value.at("nativeFormat");for(size_t i=0;i<32;i++)at<uint8_t>(parameter,8+i)=uint8_t(std::stoul(bytes.substr(i*2,2),nullptr,16));return;}
        if(value.is_object()&&value.contains("binary")){auto bytes=value.at("binary").get<std::string>();if(bytes.size()%2)throw std::runtime_error("Invalid binary body");auto length=uint32_t(bytes.size()/2);auto data=allocate(std::max(size_t(1),bytes.size()/2));for(size_t i=0;i<length;i++)at<uint8_t>(data,i)=uint8_t(std::stoul(bytes.substr(i*2,2),nullptr,16));
            if(at<uint16_t>(parameter)==DSKJADEBINARY){auto status=paramSetBinary(*reinterpret_cast<ShortDskParam*>(parameter),reinterpret_cast<BYTE*>(data),length);if(status)throw std::runtime_error("Native binary assignment failed: "+std::to_string(status));}else{auto& target=*reinterpret_cast<ShortDskParam*>(parameter);target.header.format=DSKBINARYBLOCKPOINTER;target.body.binaryBlockPtr.bytePtr=reinterpret_cast<BYTE*>(data);target.body.binaryBlockPtr.offset=0;target.body.binaryBlockPtr.length=length;target.body.binaryBlockPtr.maxLength=length;}return;}
        if(value.is_object()&&value.contains("utf8")){auto text=value.at("utf8").get<std::string>();auto data=allocate(text.size()+1);std::memcpy(reinterpret_cast<void*>(data),text.c_str(),text.size()+1);StringUtf8Info info{};info.utf8BytePtr=reinterpret_cast<StringUtf8Byte*>(data);info.size=uint32_t(text.size());info.length=native_utf8_length(text);info.maxLength=info.length+1;auto& target=*reinterpret_cast<ShortDskParam*>(parameter);auto status=target.header.format==DSKJADESTRINGUTF8?paramSetStringUtf8(target,info):paramSetStringUtf8Block(target,&info);if(status)throw std::runtime_error("Native UTF-8 assignment failed: "+std::to_string(status));return;}
        if(value.is_string()){auto text=wide(value.get<std::string>());auto length=uint32_t(text.size());auto memory=allocate((text.size()+1)*2);std::memcpy(reinterpret_cast<void*>(memory),text.c_str(),(text.size()+1)*2);
            if(at<uint16_t>(parameter)==25){auto data=memory;auto status=invoke<int>(at<word>(parameter,8),uint8_t(0x12),at<word>(parameter,24),&data,&length);if(status)throw std::runtime_error("Native string assignment failed");}
            else{at<uint16_t>(parameter)=1;at<word>(parameter,8)=memory;}return;}
        if(value.is_boolean()){at<uint16_t>(parameter)=7;at<uint8_t>(parameter,8)=value.get<bool>();return;}
        if(value.is_number_float()){at<uint16_t>(parameter)=DSKREAL;at<double>(parameter,8)=value.get<double>();return;}
        if(value.is_number_integer()){auto number=value.get<int64_t>();if(!tag&&(number<INT32_MIN||number>INT32_MAX)){at<uint16_t>(parameter)=DSKINTEGER64;at<int64_t>(parameter,8)=number;return;}at<uint16_t>(parameter)=tag?tag:4;at<int32_t>(parameter,8)=value.get<int32_t>();return;}
        if(value.is_object()&&value.contains("oid")){at<uint16_t>(parameter)=3;auto bytes=oid_bytes(value["oid"]);std::memcpy(reinterpret_cast<void*>(parameter+8),bytes.data(),22);return;}
        throw std::runtime_error("Unsupported native value assignment");
    }
    NativeValue native_snapshot(word node){auto parameter=snapshot(node);return NativeValue::capture(reinterpret_cast<word>(parameter.data()));}
    void set_node(word node,const NativeValue& value){auto status=invoke<int>(exported(interpreter,"?setValue@@YAHPEAVNode@@AEAUShortDskParam@@@Z"),node,reinterpret_cast<void*>(value.address()));if(status)throw std::runtime_error("Native node assignment failed: "+std::to_string(status));}
    word create_receiver(const std::string& oid){auto buffer=allocate(72);at<uint32_t>(buffer)=72;auto bytes=oid_bytes(oid);std::memcpy(reinterpret_cast<void*>(buffer+50),bytes.data(),22);objects[oid]=buffer;buffer_ids[buffer]=oid;return buffer;}
    void bind_receiver(word root,word receiver){std::array<unsigned char,40> self{};at<uint16_t>(reinterpret_cast<word>(self.data()))=26;at<word>(reinterpret_cast<word>(self.data()),8)=receiver;invoke<void>(interp+0xa6ac0,root,self.data());for(auto node:self_references[root])invoke<void>(interp+0x22ea0,node,receiver);}
    void abort_roots(){for(auto at=active_roots.rbegin();at!=active_roots.rend();++at)invoke<void>(interp+0xa5ec0,*at);}
    json& class_number(uint32_t number){number&=0x3fffff;for(auto& [oid,type]:types)if(!type.value("primitive",false)&&type.value("category",2)!=7&&type.value("category",2)!=8&&type["number"]==number)return type;throw std::runtime_error("Local class metadata missing for number "+std::to_string(number));}
    json& receiver_class(const std::string& oid){auto bytes=oid_bytes(oid);return class_number(at<uint32_t>(reinterpret_cast<word>(bytes.data()),8));}
    bool kind_of(const std::string& receiver,const std::string& target){auto current=receiver_class(receiver)["oid"].get<std::string>();std::set<std::string> seen;while(types.contains(current)&&seen.insert(current).second){if(current==target||string_value(types.at(current),"root_type_oid")==target)return true;current=string_value(types.at(current),"superclass_oid");}return false;}
    std::string resolve_method(const std::string& receiver,word selector){auto current=receiver_class(receiver)["oid"].get<std::string>();auto format=at<uint16_t>(selector);std::string name;uint32_t class_no=0,number=0;
        if(format==1||format==48)name=utf8(reinterpret_cast<wchar_t*>(at<word>(selector,8)));
        else if(format==4)number=at<uint32_t>(selector,8);
        else if(format==46){class_no=at<uint32_t>(selector,16);number=at<uint16_t>(selector,20);}
        else throw std::runtime_error("Unsupported method selector");
        const auto dynamic_type=current;
        while(types.contains(current)){auto& type=types.at(current);if(!class_no||type["number"]==class_no)for(const auto& method:type["methods"])if(name.empty()?method["number"]==number:method["name"]==name){
            // A feature selector identifies the declaration; ordinary sends
            // dispatch its name through the actual receiver's hierarchy.
            return find_method(dynamic_type,method["name"]);
        }current=string_value(type,"superclass_oid");}return {};
    }
    json& field_info(word selector){if(at<uint16_t>(selector)!=46)throw std::runtime_error("Unsupported property selector");auto& type=class_number(at<uint32_t>(selector,16));auto number=at<uint16_t>(selector,20);for(const auto& field:type["properties"])if(field["number"]==number)return properties.at(field["oid"]);throw std::runtime_error("Local property metadata missing");}
    std::string save_tree(word root){auto stream=allocate(48),table=allocate(8248),error=allocate(4096);invoke<word>(interp+0x58270,stream);invoke<word>(interp+0x83820,table);invoke<void>(interp+0x5cdb0,stream);
        auto status=invoke<int>(interp+0x92a00,stream,root,table,error);uint32_t size{};auto data=invoke<word>(interp+0x7ec40,stream,&size);if(status||!data||size>32*1024*1024)throw std::runtime_error("Native tree serialization failed");std::string result(reinterpret_cast<char*>(data),size);invoke<void>(interp+0x838c0,table);invoke<void>(interp+0x58930,stream);return result;}
    word load_tree(const std::string& oid){auto found=templates.find(oid);if(found==templates.end())found=templates.emplace(oid,save_tree(roots.at(oid))).first;auto& code=found->second;auto stream=allocate(48),handles=allocate(48),strings=allocate(48),error=allocate(4096);word root{},nodes{};
        at<word>(handles)=at<word>(roots.at(oid),32);at<word>(strings)=at<word>(handles);invoke<word>(interp+0x58270,stream);auto status=invoke<int>(interp+0x5d2b0,stream,code.data(),uint32_t(code.size()));auto id=oid_bytes(oid);
        if(!status)status=invoke<int>(interp+0x8ed30,id.data(),error,stream,&root,&nodes,handles,strings,false,false,false);invoke<void>(interp+0x58930,stream);if(status||!root)throw std::runtime_error("Native tree activation load failed: "+std::to_string(status));
        if(nodes){auto count=at<uint32_t>(nodes,24);auto data=at<word>(nodes,8);if(count>1000000)throw std::runtime_error("Native tree node count is invalid");for(uint32_t i=0;i<count;i++){auto node=at<word>(data,i*8);if(node&&at<word>(at<word>(node),8)==interp+0x22d10)self_references[root].push_back(node);}if(context.request.value("traceNative",false))std::cerr<<"Activation node count="<<count<<" self references="<<self_references[root].size()<<"\n";}
        return root;
    }
    void invoke_nodes(const std::string& method,const std::string& receiver,word actuals){
        if(!roots.contains(method))throw std::runtime_error("No executable source for "+methods.at(method)["name"].get<std::string>());
        auto root=load_tree(method),data=at<word>(actuals,8);auto count=at<uint32_t>(actuals,24);auto array=allocate(48),slots=allocate(count*8);std::memcpy(reinterpret_cast<void*>(array),reinterpret_cast<void*>(actuals),48);std::memcpy(reinterpret_cast<void*>(slots),reinterpret_cast<void*>(data),count*8);at<word>(array,8)=slots;
        for(uint32_t i=2;i<count;++i){auto argument=at<word>(data,i*8);if(i-2<methods.at(method)["parameters"].size()&&methods.at(method)["parameters"][i-2].value("usage",0)>=2)continue;auto owned=native_snapshot(argument);auto parameter=snapshot(argument);if(context.request.value("traceNative",false))std::cerr<<"Call argument "<<i<<" "<<hex(parameter.data(),40)<<"\n";auto clone=invoke<word>(interp+0xb8a10,argument);auto table=at<word>(clone);auto status=invoke<int>(at<word>(table,0xb8),clone,reinterpret_cast<void*>(owned.address()),uint32_t(0),uint32_t(0));if(status)throw std::runtime_error("Native argument capture failed");at<word>(slots,i*8)=clone;}
        bind_receiver(root,objects.at(receiver));auto before=service_errors.size()+failures.size();auto status=invoke<int>(interp+0xa58a0,root,array);if(status)throw std::runtime_error("Native method parameter binding failed");if(before!=service_errors.size()+failures.size())return;
        bind_receiver(root,objects.at(receiver));frames.emplace_back(method,receiver);active_roots.push_back(root);try{invoke<bool>(interp+0xa5c00,root);}catch(...){active_roots.pop_back();frames.pop_back();disarm_root(root);throw;}active_roots.pop_back();frames.pop_back();disarm_root(root);
    }
    word empty_actuals(){auto array=allocate(48),slots=allocate(16);at<word>(array,8)=slots;at<uint32_t>(array,24)=2;at<uint32_t>(array,32)=2;return array;}
    word parameter_nodes(const std::string& method,word parameters){
        if(at<uint16_t>(parameters)==32)return at<word>(parameters,8);
        std::vector<word> inputs;if(at<uint16_t>(parameters)==30){auto count=at<uint16_t>(parameters,8);for(uint16_t i=0;i<count;i++)inputs.push_back(at<word>(parameters,16+i*8));}else inputs.push_back(parameters);
        if(inputs.size()!=methods.at(method)["parameters"].size())throw std::runtime_error("Constructor argument count mismatch");
        auto array=allocate(48),nodes=allocate((inputs.size()+2)*8);auto result=invoke<word>(interp+0x23ad0,roots.at(method),uint32_t(1));at<word>(nodes,8)=invoke<word>(interp+0xb8a10,result);at<word>(array,8)=nodes;at<uint32_t>(array,24)=uint32_t(inputs.size()+2);at<uint32_t>(array,32)=uint32_t(inputs.size()+2);
        for(size_t i=0;i<inputs.size();i++){auto formal=invoke<word>(interp+0x23ad0,roots.at(method),uint32_t(i+3));auto& type=types.at(methods.at(method)["parameters"][i]["type_oid"]);auto node=!type.value("primitive",false)&&type.value("category",2)!=7?invoke<word>(exported(interpreter,"?createObjectReferenceNode@@YAPEAVNode@@H_N0@Z"),int(type["number"]),false,false):invoke<word>(interp+0xb8a10,formal);auto status=invoke<int>(exported(interpreter,"?setValue@@YAHPEAVNode@@AEAUShortDskParam@@@Z"),node,reinterpret_cast<void*>(inputs[i]));if(status)throw std::runtime_error("Constructor argument assignment failed");at<word>(nodes,(i+2)*8)=node;if(context.request.value("traceNative",false)){auto value=snapshot(node);std::cerr<<"Bound argument "<<i<<" input="<<hex(reinterpret_cast<void*>(inputs[i]),40)<<" value="<<hex(value.data(),40)<<"\n";}}return array;
    }
    void object_lifecycle(const std::string& receiver,bool creating,word parameters=0){
        std::vector<std::string> chain;auto current=receiver_class(receiver)["oid"].get<std::string>();std::set<std::string> seen;
        while(types.contains(current)&&seen.insert(current).second){chain.push_back(current);current=string_value(types.at(current),"superclass_oid");}
        if(creating)std::reverse(chain.begin(),chain.end());auto effective_constructor=find_method(receiver_class(receiver)["oid"],"create");
        for(const auto& id:chain)for(const auto& method:types.at(id)["methods"])if(method.value("source_owned",false)&&method["name"]==(creating?"create":"delete")){
            if(!method["parameters"].empty()){if(!creating)throw std::runtime_error("Destructor parameters are invalid");if(method["oid"]!=effective_constructor)continue;if(!parameters)throw std::runtime_error("Constructor arguments are required");invoke_nodes(method["oid"],receiver,parameter_nodes(method["oid"],parameters));}
            else invoke_nodes(method["oid"],receiver,empty_actuals());
        }
    }
    std::vector<NativeValue> call_arguments(word actuals){std::vector<NativeValue> result;if(!actuals)return result;auto format=at<uint16_t>(actuals);
        if(format==32){auto array=at<word>(actuals,8),nodes=at<word>(array,8);auto count=at<uint32_t>(array,24);for(uint32_t i=2;i<count;i++)result.push_back(native_snapshot(at<word>(nodes,i*8)));}
        else if(format==DSKPARAMLIST){auto count=at<uint16_t>(actuals,8);for(uint16_t i=0;i<count;i++)result.push_back(NativeValue::capture(at<word>(actuals,16+i*8)));}
        else result.push_back(NativeValue::capture(actuals));return result;
    }
    void call_result(word actuals,word output,const NativeValue& value){if(actuals&&at<uint16_t>(actuals)==32){auto array=at<word>(actuals,8),nodes=at<word>(array,8);auto target=at<word>(nodes,8);if(target)set_node(target,value);}if(output)value.copy_to(output);}
    word raise_runtime_error(int code){auto type=type_named("RootSchema","SystemException");auto oid=identity(types.at(type)["number"].get<uint32_t>()|0x400000,next_instance++);create_receiver(oid);values[{oid,find_property(type,"errorCode")}]=code;values[{oid,find_property(type,"resumable")}]=true;auto bytes=oid_bytes(oid);std::array<word,10> arguments{};arguments[1]=reinterpret_cast<word>(bytes.data());return service("jomRaiseException",arguments);}
    word service(const std::string& name,const std::array<word,10>& a){
        if(context.request.value("traceNative",false))std::cerr<<"Runtime service "<<name<<"\n";
        if(name=="jomArmExceptionHandler"){
            auto receiver=hex(reinterpret_cast<void*>(a[1]),22),target=hex(reinterpret_cast<void*>(a[2]),22),method=resolve_method(receiver,a[3]);if(method.empty()||!roots.contains(method))throw std::runtime_error("Exception handler source is missing");
            word callback{};if(a[4]&&at<uint16_t>(a[4])){if(at<uint16_t>(a[4])!=DSKINTERPPARAMCALLBACK)throw std::runtime_error("Unsupported exception handler parameter format");callback=allocate(sizeof(DskParam));std::memcpy(reinterpret_cast<void*>(callback),reinterpret_cast<void*>(a[4]),sizeof(DskParam));}
            exception_handlers.push_back({receiver,target,method,active_roots.empty()?0:active_roots.back(),callback,uint8_t(a[5])!=0,true,++next_handler_token});return 0;
        }
        if(name=="jomDisarmExceptionHandler"){auto target=hex(reinterpret_cast<void*>(a[1]),22);for(size_t i=exception_handlers.size();i>0;i--)if(exception_handlers[i-1].target==target&&exception_handlers[i-1].global==(uint8_t(a[2])!=0)){exception_handlers.erase(exception_handlers.begin()+i-1);break;}return 0;}
        if(name=="jomRaiseException"){
            if(context.request.value("traceNative",false)){void* stack[32]{};auto count=native_backtrace(0,32,stack,nullptr);for(USHORT i=0;i<count;i++){HMODULE owner{};wchar_t path[MAX_PATH]{};if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(stack[i]),&owner)){GetModuleFileNameW(owner,path,MAX_PATH);std::cerr<<"Exception frame "<<filename(fs::path(path).filename())<<" +0x"<<std::hex<<(reinterpret_cast<word>(stack[i])-reinterpret_cast<word>(owner))<<std::dec<<"\n";}}}

            if(!a[1])throw std::runtime_error("Native exception has no local receiver");auto oid=hex(reinterpret_cast<void*>(a[1]),22);
            if(!objects.contains(oid))throw std::runtime_error("Native exception receiver is not local: "+oid);
            auto type=receiver_class(oid)["oid"].get<std::string>();auto error_field=find_property(type,"errorCode");auto value=values.find({oid,error_field});auto code=value==values.end()?0:int(value->second.integer());bool expected=false;
            std::vector<ExceptionHandler> ordered;for(bool global:{false,true})for(auto it=exception_handlers.rbegin();it!=exception_handlers.rend();++it)if(it->global==global)ordered.push_back(*it);
            for(auto handler:ordered){auto found=std::find_if(exception_handlers.begin(),exception_handlers.end(),[&](const auto& row){return row.token==handler.token;});if(found==exception_handlers.end()||!found->active||!kind_of(oid,handler.target))continue;found->active=false;
                auto list=allocate(sizeof(DskParam)),argument=allocate(40);write_value(argument,json{{"oid",oid}});at<uint16_t>(list)=DSKPARAMLIST;at<uint16_t>(list,8)=1;at<word>(list,16)=argument;
                if(handler.callback){auto& callback=*reinterpret_cast<DskParam*>(handler.callback);auto extra=allocate(sizeof(DskParam));auto pointer=reinterpret_cast<DskParam*>(extra);callback.body.interpParamCallBack.pCallBack(&callback,&pointer,nullptr);extra=reinterpret_cast<word>(pointer);if(at<uint16_t>(extra)!=DSKPARAMLIST)throw std::runtime_error("Exception handler arguments were not a parameter list");auto count=at<uint16_t>(extra,8);if(count>48)throw std::runtime_error("Exception handler argument limit exceeded");if(!count)throw std::runtime_error("Exception handler parameter list has no exception slot");for(uint16_t n=1;n<count;n++)at<word>(list,16+n*8)=at<word>(extra,16+n*8);at<uint16_t>(list,8)=count;}
                auto nodes=parameter_nodes(handler.method,list);invoke_nodes(handler.method,handler.receiver,nodes);auto result=snapshot(at<word>(at<word>(nodes,8),8));auto disposition=read_value(reinterpret_cast<word>(result.data()));
                if(auto active=std::find_if(exception_handlers.begin(),exception_handlers.end(),[&](const auto& row){return row.token==handler.token;});active!=exception_handlers.end())active->active=true;
                if(disposition==-1)continue;if(disposition==0){auto field=find_property(type,"continuable");auto flag=values.find({oid,field});if(flag==values.end()||!flag->second.boolean())throw std::runtime_error("Jade 1238: exception is not continuable");return 0;}if(disposition==3){auto field=find_property(type,"resumable");auto flag=values.find({oid,field});if(flag==values.end()||!flag->second.boolean())throw std::runtime_error("Jade 1238: exception is not resumable");if(handler.global){abort_roots();return 0;}auto root=std::find(active_roots.begin(),active_roots.end(),handler.root);if(root==active_roots.end())throw std::runtime_error("Exception handler activation is no longer running");for(;root!=active_roots.end();++root)invoke<void>(interp+0xa5ec0,*root);return 0;}if(disposition==2){if(handler.global){abort_roots();return 0;}auto field=find_property(type,"resumable");auto flag=values.find({oid,field});if(flag==values.end()||!flag->second.boolean())throw std::runtime_error("Jade 1238: exception is not resumable");throw ResumeStatement{handler.root};}if(disposition==1){abort_roots();return 0;}
                throw std::runtime_error("Exception handler return disposition is unsupported or invalid: "+disposition.dump());
            }

            for(const auto& entry:expected_exceptions)if(entry.is_integer()?entry.integer()==code:entry.is_reference()&&kind_of(oid,entry.reference()))expected=true;
            if(expected)++matched_exceptions;else failures.push_back({{"operation","raise"},{"message","Unexpected Jade exception "+std::to_string(code)},{"errorCode",code},{"values",json::array({code})}});
            abort_roots();return 0;
        }
        if(name=="jomCallPrimMethod"){
            const json* type=nullptr;for(const auto& [id,row]:types)if(row.value("primitive",false)&&row["number"]==a[1]){type=&row;break;}
            if(!type)throw std::runtime_error("Primitive receiver metadata missing");
            auto format=at<uint16_t>(a[3]);std::string method_name;uint32_t number=0;
            if(format==1||format==48)method_name=utf8(reinterpret_cast<wchar_t*>(at<word>(a[3],8)));
            else if(format==4)number=at<uint32_t>(a[3],8);else if(format==46)number=at<uint16_t>(a[3],20);else throw std::runtime_error("Unsupported primitive selector: "+std::to_string(format));
            const json* method=nullptr;for(const auto& row:(*type)["methods"])if(number?row["number"]==number:row["name"]==method_name){method=&row;break;}
            if(!method)throw std::runtime_error("Primitive method absent from cache");method_name=(*method)["name"];
            auto type_name=(*type)["name"].get<std::string>(),routine=type_name;routine[0]=char(std::tolower(routine[0]));auto suffix=method_name;suffix[0]=char(std::toupper(suffix[0]));routine+=suffix;
            if(routine=="dateYear")routine="dateGetYear";
            static const std::set<std::string> verified={"stringAsDate","dateSetDate","dateSetDateYearAbsolute","dateGetYear","timeStampSetDate","timeStampDate"};
            auto entry=method->value("native_entrypoint",std::string{}),library=method->value("native_library",std::string{});std::transform(library.begin(),library.end(),library.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(!entry.empty()&&(library=="jomsupp"||library=="jomsupp.dll"))routine=entry;
            else if(!verified.contains(routine))throw std::runtime_error("Native entrypoint metadata missing for "+type_name+"::"+method_name+"; refresh the system cache");
            if(!a[4]||at<uint16_t>(a[4])!=32){std::array<unsigned char,40> ignored{};auto status=invoke<int>(exported(primitive_library,routine.c_str()),word(0),a[2],a[4],a[5]?a[5]:reinterpret_cast<word>(ignored.data()));if(status)throw std::runtime_error("Native primitive failed: "+routine+" status="+std::to_string(status));return 0;}
            auto array=at<word>(a[4],8),nodes=at<word>(array,8);auto count=at<uint32_t>(array,24);std::vector<std::array<unsigned char,40>> parameters(count);std::vector<word> pointers(count);
            if(context.request.value("traceNative",false))std::cerr<<"Primitive "<<routine<<" receiver="<<at<uint16_t>(a[2])<<" count="<<count<<" output="<<a[5]<<"\n";
            for(uint32_t i=0;i<count;i++){auto node=at<word>(nodes,i*8);if(node)parameters[i]=snapshot(node);pointers[i]=reinterpret_cast<word>(parameters[i].data());}
            if(count<2)throw std::runtime_error("Primitive result node missing");
            auto output=a[5]?a[5]:pointers[1];
            if(context.request.value("traceNative",false))std::cerr<<"Primitive invoking "<<routine<<" outputTag="<<at<uint16_t>(output)<<"\n";
            auto result=invoke<int>(exported(primitive_library,routine.c_str()),word(0),a[2],pointers.data(),output);
            if(context.request.value("traceNative",false))std::cerr<<"Primitive returned "<<routine<<" status="<<result<<"\n";
            if(result)throw std::runtime_error("Native primitive failed: "+routine+" status="+std::to_string(result));
            if(!a[5]&&at<word>(nodes,8)){auto status=invoke<int>(exported(interpreter,"?setValue@@YAHPEAVNode@@AEAUShortDskParam@@@Z"),at<word>(nodes,8),parameters[1].data());if(status)throw std::runtime_error("Primitive result assignment failed");}
            for(uint32_t i=2;i<count;i++)if(i-2<(*method)["parameters"].size()&&(*method)["parameters"][i-2].value("usage",0)>=2){auto node=at<word>(nodes,i*8);auto status=invoke<int>(exported(interpreter,"?setValue@@YAHPEAVNode@@AEAUShortDskParam@@@Z"),node,parameters[i].data());if(status)throw std::runtime_error("Primitive output assignment failed");}
            return 0;
        }
        if(name=="jomCreateObject"){auto oid=oid_bytes(hex(reinterpret_cast<void*>(a[1]),22));std::memcpy(oid.data(),&next_instance,8);at<uint32_t>(reinterpret_cast<word>(oid.data()),8)|=0x400000;++next_instance;auto identity=hex(oid.data(),22);create_receiver(identity);std::memcpy(reinterpret_cast<void*>(a[1]),oid.data(),22);object_lifecycle(identity,true,a[3]);return 0;}
        if(name=="jomDeleteObject"||name=="jomDeleteObjectNoException"){auto oid=hex(reinterpret_cast<void*>(a[1]),22);if(!objects.contains(oid))throw std::runtime_error("Delete of unknown local object");object_lifecycle(oid,false);buffer_ids.erase(objects.at(oid));objects.erase(oid);collection_values.erase(oid);dictionary_keys.erase(oid);for(auto at=values.begin();at!=values.end();)if(at->first.first==oid)at=values.erase(at);else++at;return 0;}
        if(name=="jomGetBufferProperty"||name=="jomSetBufferProperty"||name=="jomGetProperty"||name=="jomSetProperty"){
            auto receiver=name.find("Buffer")!=std::string::npos?buffer_ids.at(a[1]):hex(reinterpret_cast<void*>(a[1]),22);if(!objects.contains(receiver))throw std::runtime_error("Unknown local property receiver: "+receiver);auto& field=field_info(a[2]);auto& data_type=types.at(field["type_oid"]);auto key=std::make_pair(receiver,field["oid"].get<std::string>());auto type_name=data_type["name"].get<std::string>();
            if(name.starts_with("jomSet")){auto value=NativeValue::capture(a[3]);if(type_name=="String"){Character* text{};Size length{};auto status=paramGetString(*reinterpret_cast<const ShortDskParam*>(value.address()),text,&length);if(status)throw std::runtime_error("Invalid String property value");if(length>field.value("length",0u))throw std::runtime_error("String exceeds property length");}values[key]=std::move(value);}
            else{if(auto found=values.find(key);found!=values.end())found->second.copy_to(a[3]);else NativeValue::zero(data_type.value("primitive",false)?data_type["number"].get<uint16_t>():uint16_t(3)).copy_to(a[3]);}return 0;
        }
        if(name=="jomGetPropertyDesc"){auto& field=field_info(a[2]);auto& type=types.at(field["type_oid"]);if(at<uint16_t>(a[3])!=28)throw std::runtime_error("Expected property descriptor output");auto output=a[3]+8;std::memset(reinterpret_cast<void*>(output),0,256);auto primitive=type.value("primitive",false);auto type_name=type["name"].get<std::string>();uint16_t format=primitive?(type_name=="String"?25:type_name=="Binary"?11:type["number"].get<uint16_t>()):3;
            at<uint32_t>(output)=primitive?2:1;at<uint32_t>(output,8)=primitive?(format==25?field.value("length",0u)*2:format==11?field.value("length",0u):format==7?1:format==18||format==21||format==42?8:format==19?16:4):22;at<uint32_t>(output,12)=field.value("length",0u);at<uint32_t>(output,16)=type["number"];at<uint32_t>(output,20)=primitive?0:type["number"].get<uint32_t>();at<uint32_t>(output,24)=type["number"];at<uint16_t>(output,28)=uint16_t(field["number"]);at<uint16_t>(output,30)=format;at<uint8_t>(output,34)=at<uint8_t>(a[2],22);at<uint8_t>(output,35)=field.value("precision",uint8_t(0));at<uint8_t>(output,36)=field.value("scaleFactor",uint8_t(0));auto oid=oid_bytes(field["oid"]);std::memcpy(reinterpret_cast<void*>(output+42),oid.data(),12);put_text(output+54,field["name"]);return 0;
        }
        if(name=="jomIsKindOf"){auto oid=hex(reinterpret_cast<void*>(a[1]),22);at<uint32_t>(a[3])=kind_of(oid,class_number(uint32_t(a[2]))["oid"]);return 0;}
        if(name=="jomIsValidObject"){at<uint32_t>(a[2])=objects.contains(hex(reinterpret_cast<void*>(a[1]),22));return 0;}
        if(name=="jomGetOidForMetaObject"){if(a[1]!=1)throw std::runtime_error("Unsupported meta-object category");auto& type=class_number(uint32_t(a[4]));auto oid=oid_bytes(type["oid"]);std::memcpy(reinterpret_cast<void*>(a[6]),oid.data(),22);return 0;}
        if(name=="jomInheritCreate"){if(frames.empty())throw std::runtime_error("No inherited constructor activation");auto current=methods.at(frames.back().first);auto parent=string_value(types.at(current["owner_oid"]),"superclass_oid");auto inherited=find_method(parent,"create");if(inherited.empty()||!roots.contains(inherited))throw std::runtime_error("No inherited constructor source");auto receiver=hex(reinterpret_cast<void*>(a[1]),22);invoke_nodes(inherited,receiver,methods.at(inherited)["parameters"].empty()?empty_actuals():parameter_nodes(inherited,a[2]));return 0;}
        if(name=="jomInheritMethod"){if(frames.empty())throw std::runtime_error("No inherited activation");auto current=methods.at(frames.back().first);auto parent=string_value(types.at(current["owner_oid"]),"superclass_oid");auto inherited=find_method(parent,current["name"]);if(inherited.empty())throw std::runtime_error("No inherited method source");if(at<uint16_t>(a[2])!=32)throw std::runtime_error("Expected inherited node arguments");invoke_nodes(inherited,frames.back().second,at<word>(a[2],8));return 0;}
        if(name=="jomSendMsgWithApp"||name=="jomSendMsgToBufferWithApp"){
            auto receiver=name=="jomSendMsgToBufferWithApp"?buffer_ids.at(a[1]):hex(reinterpret_cast<void*>(a[1]),22);auto method=resolve_method(receiver,a[2]);if(method.empty())throw std::runtime_error("Method is missing from local metadata");auto& metadata=methods.at(method);auto& owner=types.at(metadata["owner_oid"]);auto method_name=metadata["name"].get<std::string>();
            if(context.request.value("traceNative",false))std::cerr<<"Runtime method "<<owner["name"]<<"::"<<method_name<<"\n";
            if(owner["name"]=="Object"&&!owner.value("source_owned",false)&&method_name=="isKindOf"&&at<uint16_t>(a[3])!=32){auto target=read_value(a[3]);write_value(a[4],kind_of(receiver,target["oid"]));return 0;}
            if(owner["name"]=="JadeTestCase"&&!owner.value("source_owned",false)&&method_name=="expectedException"){auto arguments=call_arguments(a[3]);for(const auto& entry:arguments){if(entry.format()==DSKPARAMLIST)for(const auto& nested:entry.elements())expected_exceptions.push_back(nested);else expected_exceptions.push_back(entry);}return 0;}
            if(receiver_class(receiver).value("category",2)==3){bool array_kind=kind_of(receiver,type_named("RootSchema","Array")),set_kind=kind_of(receiver,type_named("RootSchema","Set")),dict_kind=kind_of(receiver,type_named("RootSchema","Dictionary"));if(!array_kind&&!set_kind&&!dict_kind)throw std::runtime_error("Unsupported collection category: "+receiver_class(receiver)["name"].get<std::string>());auto arguments=call_arguments(a[3]);auto& items=collection_values[receiver];NativeValue result;
                if(context.request.value("traceNative",false))std::cerr<<"Collection arguments "<<arguments.size()<<" items="<<items.size()<<"\n";
                if(dict_kind&&(method_name=="putAtKey"||method_name=="getAtKey"||method_name=="includesKey"||method_name=="removeKey")){
                    auto definition=receiver_class(receiver);if(!definition.contains("keys")||definition["keys"].empty())throw std::runtime_error("Dictionary key metadata missing; refresh the system cache");auto key_definitions=definition["keys"];std::vector<NativeValue> inputs=arguments;if(!inputs.empty()&&inputs[0].format()==DSKPARAMLIST){auto packed=inputs.front().elements();inputs.erase(inputs.begin());inputs.insert(inputs.begin(),packed.begin(),packed.end());}bool adding=method_name=="putAtKey";if(inputs.size()!=key_definitions.size()+(adding?1:0))throw std::runtime_error("Dictionary key argument count mismatch");std::vector<NativeValue> lookup;for(size_t i=0;i<key_definitions.size();i++)lookup.push_back(inputs[i]);
                    auto compare=[&](const std::vector<NativeValue>& left,const std::vector<NativeValue>& right){for(size_t i=0;i<key_definitions.size();i++){const auto& key=key_definitions[i];if(key.value("sort_order",0)!=0)throw std::runtime_error("Offline dictionary locale collation is unsupported");int order=0;if(left[i].is_string()&&right[i].is_string()){auto x=left[i].string(),y=right[i].string();auto compared=CompareStringOrdinal(x.data(),int(x.size()),y.data(),int(y.size()),key.value("case_insensitive",false));if(!compared)throw std::runtime_error("Dictionary key comparison failed");order=compared-CSTR_EQUAL;}else if(left[i].is_integer()&&right[i].is_integer())order=left[i].integer()<right[i].integer()?-1:left[i].integer()>right[i].integer()?1:0;else if(left[i]!=right[i])throw std::runtime_error("Unsupported offline dictionary key comparison");if(order)return key.value("descending",false)?-order:order;}return 0;};
                    auto& stored=dictionary_keys[receiver];size_t position=0;while(position<stored.size()&&compare(stored[position],lookup)<0)++position;bool found=position<stored.size()&&compare(stored[position],lookup)==0;
                    if(adding){if(found)return raise_runtime_error(1310);stored.insert(stored.begin()+position,lookup);items.insert(items.begin()+position,inputs.back());}
                    else if(method_name=="includesKey")result=found;
                    else if(method_name=="getAtKey")result=found?items[position]:NativeValue::zero(DSKOBJECTID);
                    else{if(!found)return raise_runtime_error(1301);result=items[position];items.erase(items.begin()+position);stored.erase(stored.begin()+position);}
                }
                else if(method_name=="add"||method_name=="tryAdd"){if(dict_kind)throw std::runtime_error("Use dictionary key operations for this collection");if(arguments.size()!=1)throw std::runtime_error("Collection.add requires one argument");bool found=std::find(items.begin(),items.end(),arguments[0])!=items.end();if(set_kind&&found&&method_name=="add")return raise_runtime_error(1309);if((array_kind&&method_name=="add")||!found)items.push_back(arguments[0]);if(method_name=="tryAdd")result=!found;}
                else if(method_name=="size")result=int32_t(items.size());
                else if(method_name=="isEmpty")result=items.empty();
                else if(method_name=="includes"){if(arguments.size()!=1)throw std::runtime_error("Collection.includes requires one argument");result=std::find(items.begin(),items.end(),arguments[0])!=items.end();}
                else if(method_name=="at"){if(!array_kind||arguments.size()!=1)throw std::runtime_error("Array.at requires an Integer index");auto item_index=arguments[0].integer();if(item_index<1||size_t(item_index)>items.size())return raise_runtime_error(1300);result=items[size_t(item_index)-1];}
                else if(method_name=="atPut"){if(!array_kind||arguments.size()!=2)throw std::runtime_error("Array.atPut requires an Integer index and a member");auto position=arguments[0].integer();if(position<1||size_t(position)>items.size())return raise_runtime_error(1300);items[size_t(position)-1]=arguments[1];}
                else if(method_name=="removeAt"){if(!array_kind||arguments.size()!=1)throw std::runtime_error("Array.removeAt requires an Integer index");auto position=arguments[0].integer();if(position<1||size_t(position)>items.size())return raise_runtime_error(1300);result=items[size_t(position)-1];items.erase(items.begin()+position-1);}
                else if(method_name=="clear"){items.clear();dictionary_keys.erase(receiver);}
                else throw std::runtime_error("Unimplemented local collection operation: "+method_name);
                if(context.request.value("traceNative",false))std::cerr<<"Collection result present="<<bool(result)<<" output="<<a[4]<<"\n";if(result)call_result(a[3],a[4],result);return 0;
            }
            if(at<uint16_t>(a[3])!=32)throw std::runtime_error("Expected native method node arguments");auto array=at<word>(a[3],8),data=at<word>(array,8);auto count=at<uint32_t>(array,24);
            if(owner["name"]=="JadeTestCase"&&!owner.value("source_owned",false)){
                static const std::set<std::string> supported={"assertEquals","assertEqualsMsg","assertTrue","assertTrueMsg","assertFalse","assertFalseMsg","assertNull","assertNullMsg","assertNotNull","assertNotNullMsg","assert","info"};if(!supported.contains(method_name))throw std::runtime_error("Unsupported core test operation: "+method_name);
                std::vector<std::array<unsigned char,40>> parameters;json arguments=json::array();for(uint32_t i=2;i<count;++i){parameters.push_back(snapshot(at<word>(data,i*8)));arguments.push_back(read_value(reinterpret_cast<word>(parameters.back().data())));}
                size_t first=method_name.ends_with("Msg")?1:0;std::string message=first?arguments.at(0).get<std::string>():std::string{};bool passed=false;
                if(method_name.starts_with("assertEquals")){bool equal=false;auto status=invoke<int>(exported(messages,"?paramsAreEqual@@YAHPEBUDskParam@@0PEA_N@Z"),parameters.at(first).data(),parameters.at(first+1).data(),&equal);if(status)throw std::runtime_error("Native comparison failed");passed=equal;}
                else if(method_name.starts_with("assertTrue"))passed=arguments.at(first)==true;
                else if(method_name.starts_with("assertFalse"))passed=arguments.at(first)==false;
                else if(method_name.starts_with("assertNull")||method_name.starts_with("assertNotNull")){auto value=arguments.at(first);bool nil=value.is_null()||(value.is_object()&&value.value("oid",std::string{})==std::string(44,'0'));passed=method_name.starts_with("assertNotNull")?!nil:nil;}
                else if(method_name=="info"){passed=true;message=arguments.at(0);}else if(!arguments.empty())message=arguments.at(0);
                json record={{"operation",method_name},{"passed",passed},{"values",arguments},{"message",message},{"stack",json::array()}};
                for(const auto& [id,obj]:frames){auto& frame=methods.at(id);record["stack"].push_back({{"owner",types.at(frame["owner_oid"])["name"]},{"method",frame["name"]},{"source",frame["source_path"]},{"line",frame["line"]}});}
                if(!frames.empty()){auto& frame=methods.at(frames.back().first);auto text=wide(frame["source"]);auto prefix=text.substr(0,std::min(text.size(),size_t(uint32_t(a[6]))));auto last=prefix.rfind(L'\n');record["source"]=frame["source_path"];record["line"]=frame["line"].get<int>()+int(std::count(prefix.begin(),prefix.end(),L'\n'));record["column"]=last==std::wstring::npos?prefix.size()+1:prefix.size()-last;}
                assertions.push_back(record);if(!passed){failures.push_back(record);abort_roots();}return 0;
            }
            if(owner["name"]=="Object"&&!owner.value("source_owned",false)&&method_name=="isKindOf"){auto value=snapshot(at<word>(data,16));auto target=read_value(reinterpret_cast<word>(value.data()));set_node(at<word>(data,8),NativeValue(kind_of(receiver,target["oid"])));return 0;}
            invoke_nodes(method,receiver,array);return 0;
        }
        throw std::runtime_error("Unsupported offline runtime service: "+name);
    }
    void hook_services(){
        // Keep native expression evaluation. A resumed local exception crosses
        // the interrupted expression and returns at its statement boundary.
        auto statements=service_hooks.callback("N_aryNode::evaluate",interp+0xa3990,[this](const std::array<word,10>& a)->word{auto block=a[0];auto root=active_roots.empty()?0:active_roots.back();at<uint16_t>(block,24)=0;while(at<uint16_t>(block,24)<at<uint32_t>(block,56)){auto node=at<word>(at<word>(block,40),size_t(at<uint16_t>(block,24))*8);try{invoke<bool>(at<word>(at<word>(node),0x78),node);}catch(const ResumeStatement& resume){if(root!=resume.root)throw;auto name=invoke<const wchar_t*>(at<word>(at<word>(node),8),node);if(name&&std::wstring_view(name).find(L"Return")!=std::wstring_view::npos){invoke<void>(interp+0xa5ec0,root);return 1;}}++at<uint16_t>(block,24);}return 1;});
        service_hooks.replace_entry(interp+0xa3990,statements);
        // The pinned interpreter builds system exceptions through database-backed
        // DskObject helpers. Preserve its selected class/code in local objects.
        auto raise_callback=service_hooks.callback("raiseAnException",interp+0x680b0,[this](const std::array<word,10>& a)->word{try{auto oid=identity(uint32_t(a[0])|0x400000,next_instance++);create_receiver(oid);auto field=find_property(receiver_class(oid)["oid"],"errorCode");if(field.empty())throw std::runtime_error("System exception errorCode metadata missing");values[{oid,field}]=int32_t(a[1]);for(auto [name,flag]:std::array<std::pair<const char*,bool>,2>{{{"resumable",bool(uint8_t(a[2]))},{"continuable",bool(uint8_t(a[3]))}}}){auto feature=find_property(receiver_class(oid)["oid"],name);if(!feature.empty())values[{oid,feature}]=flag;}auto bytes=oid_bytes(oid);std::array<word,10> call{};call[1]=reinterpret_cast<word>(bytes.data());call[2]=a[2];call[3]=a[5];return service("jomRaiseException",call);}catch(const std::exception& error){service_errors.push_back({{"operation","raiseAnException"},{"error",error.what()}});abort_roots();return 1219;}});
        service_hooks.replace_entry(interp+0x680b0,raise_callback);
        for(const auto& item:imports(interpreter))if(item.name.starts_with("jom")||item.name.find("@Iterator@@")!=std::string::npos||item.name.starts_with("?isLockedByMe@DskObject@@")||item.name.starts_with("?lockObject@DskObject@@")||item.name.starts_with("?unlockObject@DskObject@@")||item.name=="?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z"){auto callback=service_hooks.callback(item.name,item.original,[this,item](const std::array<word,10>& args)->word{try{
            if(item.name.find("@Iterator@@")!=std::string::npos)return iterator_service(item.name,args);
            if(item.name.starts_with("?isLockedByMe@DskObject@@")||item.name.starts_with("?lockObject@DskObject@@")||item.name.starts_with("?unlockObject@DskObject@@")){auto oid=oid_of(args[0]);if(!objects.contains(oid))throw std::runtime_error("Lock receiver is not local");return item.name.starts_with("?isLockedByMe")?1:0;}
            if(item.name.starts_with("?sendMsg")){std::array<unsigned char,40> selector{};write_value(reinterpret_cast<word>(selector.data()),utf8(reinterpret_cast<wchar_t*>(args[1])));std::array<word,10> call{};call[1]=args[0]+16;call[2]=reinterpret_cast<word>(selector.data());call[3]=args[2];call[4]=args[3];call[6]=args[4];return service("jomSendMsgWithApp",call);}
            return service(item.name,args);}catch(const std::exception& error){service_errors.push_back({{"operation",item.name},{"error",error.what()}});abort_roots();return 1219;}});service_hooks.replace(item.slot,callback);}
    }
    json with_execution_context(const std::function<json()>& action){
        auto native_context=allocate(616);if(!cache_manager){cache_manager=native_alloc(320);std::memset(reinterpret_cast<void*>(cache_manager),0,320);invoke<word>(interp+0x77390,cache_manager);}at<word>(native_context,336)=cache_manager;
        auto slot=TlsAlloc();if(slot==TLS_OUT_OF_INDEXES)throw std::runtime_error("Native TLS allocation failed");auto& native_slot=at<DWORD>(interp+0x197090);auto previous=native_slot;native_slot=slot;
        struct Cleanup {word interp;DWORD previous,slot;~Cleanup(){invoke<void>(interp+0x58490,word(0));at<DWORD>(interp+0x197090)=previous;TlsFree(slot);}}cleanup{interp,previous,slot};
        invoke<void>(interp+0x58490,native_context);return action();
    }
    void lifecycle(const std::string& receiver,bool creating){with_execution_context([&]()->json{object_lifecycle(receiver,creating);return nullptr;});}
    json evaluate(word root,word receiver,const json& arguments=json::array(),const std::string& activation_method={}){return with_execution_context([&]()->json{if(!activation_method.empty())root=load_tree(activation_method);
        auto array=allocate(48),nodes=allocate((2+arguments.size())*8);at<word>(array)=at<word>(root,32);at<word>(array,8)=nodes;at<uint32_t>(array,24)=uint32_t(2+arguments.size());at<uint32_t>(array,32)=uint32_t(2+arguments.size());
        const auto& declaration=methods.at(activation_method);if(arguments.size()!=declaration["parameters"].size())throw std::runtime_error("Top-level argument count mismatch");
        for(size_t i=0;i<arguments.size();++i){auto formal=invoke<word>(interp+0x23ad0,root,uint32_t(i+3));auto node=invoke<word>(interp+0xb8a10,formal);std::array<word,5> input{};write_value(reinterpret_cast<word>(input.data()),arguments[i]);auto value=NativeValue::capture(reinterpret_cast<word>(input.data()));set_node(node,value);at<word>(nodes,(i+2)*8)=node;}
        auto formal=invoke<word>(interp+0x23ad0,root,uint32_t(1));auto result_node=invoke<word>(interp+0xb8a10,formal);at<word>(nodes,8)=result_node;
        bind_receiver(root,receiver);auto binding=invoke<int>(interp+0xa58a0,root,array);if(binding)throw std::runtime_error("Native argument binding failed: "+std::to_string(binding));
        bind_receiver(root,receiver);active_roots.push_back(root);try{invoke<bool>(interp+0xa5c00,root);}catch(...){active_roots.pop_back();disarm_root(root);throw;}active_roots.pop_back();disarm_root(root);auto result=snapshot(result_node);return read_value(reinterpret_cast<word>(result.data()));});
    }
    word allocate(size_t size){auto block=std::make_unique<unsigned char[]>(size);std::memset(block.get(),0,size);auto value=reinterpret_cast<word>(block.get());arena.push_back(std::move(block));return value;}
    word native_alloc(size_t size){return invoke<word>(exported(jom,"??2JomObj@@SAPEAX_K@Z"),size);}
    word wrap(const std::string& kind,const std::string& oid){auto value=native_alloc(40);invoke<word>(exported(jom,("??0"+kind+"@@QEAA@XZ").c_str()),value);put_oid(value,oid);return value;}
    void collection(word output,const std::vector<std::string>& oids){auto add=at<word>(at<word>(output),64);for(const auto& oid:oids){auto value=oid_bytes(oid);invoke<void>(add,output,value.data());}}
    std::string type_named(const std::string& schema,const std::string& name){auto found=visible[schema].find(name);if(found==visible[schema].end())throw std::runtime_error("Type source/system definition missing: "+schema+"::"+name);return found->second;}
    json* member(const std::string& oid){if(methods.contains(oid))return &methods.at(oid);if(usages.contains(oid))return &usages.at(oid);if(properties.contains(oid))return &properties.at(oid);if(constants.contains(oid))return &constants.at(oid);if(keys.contains(oid))return &keys.at(oid);return nullptr;}
    std::string find_global_constant(std::string schema,const std::string& name){std::set<std::string> seen;while(schemas.contains(schema)&&seen.insert(schema).second){for(const auto& constant:schemas.at(schema).value("constants",json::array()))if(constant["name"]==name)return constant["oid"];schema=string_value(schemas.at(schema),"superschema_oid");}return {};}
    std::string find_constant(std::string oid,const std::string& name){std::set<std::string> seen;while(types.contains(oid)&&seen.insert(oid).second){for(const auto& [id,row]:constants)if(row["owner_oid"]==oid&&row["name"]==name)return id;oid=string_value(types.at(oid),"superclass_oid");}return {};}
    word type_wrapper(const std::string& oid){const auto& type=types.at(oid);auto category=type.value("category",2);return wrap(type.value("primitive",false)?"DskPrimType":category==3?"DskCollClass":category==7?"DskPseudoType":category==8?"DskJadeInterface":"DskClass",oid);}
    word metadata_property(const std::array<word,10>& a){
        auto oid=hex(reinterpret_cast<void*>(a[1]),22);if(objects.contains(oid))return service("jomGetProperty",a);
        json* row=member(oid);if(!row&&types.contains(oid))row=&types.at(oid);if(!row&&schemas.contains(oid))row=&schemas.at(oid);if(!row)throw std::runtime_error("Uncached metadata property receiver "+oid);
        std::string field;auto format=at<uint16_t>(a[2]);
        if(format==46){auto& owner=class_number(at<uint32_t>(a[2],16));auto number=at<uint16_t>(a[2],20);for(const auto& property:owner["properties"])if(property["number"]==number){field=property["name"];break;}}
        else if(format==1||format==48)field=utf8(reinterpret_cast<wchar_t*>(at<word>(a[2],8)));
        if(row->contains("native_properties")&&row->at("native_properties").contains(field)){write_value(a[3],row->at("native_properties").at(field),at<uint16_t>(a[3]));return 0;}
        if(field=="superschemaType"){write_value(a[3],json{{"oid",std::string(44,'0')}});return 0;}
        if(field=="version"){write_value(a[3],0,at<uint16_t>(a[3]));return 0;}
        if(field=="options"){write_value(a[3],(row->value("type_method",false)?4:0)|(row->value("receiver_by_reference",false)?8:0),at<uint16_t>(a[3]));return 0;}
        if(field=="executionLocation"||field=="unitTestFlags"){write_value(a[3],row->value(field,0),at<uint16_t>(a[3]));return 0;}
        static const std::map<std::string,std::string> names={{"memberType","member_type_oid"},{"memberTypeSize","member_type_size"},{"memberTypePrecision","member_type_precision"},{"memberTypeSFactor","member_type_scale"},{"schema","schema_oid"},{"type","type_oid"},{"schemaType","owner_oid"},{"superclass","superclass_oid"},{"subAccess","subaccess"},{"subschemaCopyFinal","subschema_copy_final"},{"subschemaFinal","subschema_final"},{"transientAllowed","transient_allowed"},{"persistentAllowed","persistent_allowed"},{"sharedTransientAllowed","shared_transient_allowed"},{"rootType","root_type_oid"}};
        if(auto found=names.find(field);found!=names.end())field=found->second;
        if(field.empty()||!row->contains(field))throw std::runtime_error("Uncached metadata property "+field+" on "+oid);
        auto value=row->at(field);if(field.ends_with("_oid"))value={{"oid",value.is_null()?std::string(44,'0'):value.get<std::string>()}};
        write_value(a[3],value,at<uint16_t>(a[3]));return 0;
    }
    std::string find_method(std::string oid,const std::string& name){std::set<std::string> seen;while(types.contains(oid)&&seen.insert(oid).second){for(const auto& method:types[oid]["methods"])if(method["name"]==name)return method["oid"];oid=string_value(types[oid],"superclass_oid");}return {};}
    std::string find_property(std::string oid,const std::string& name){std::set<std::string> seen;while(types.contains(oid)&&seen.insert(oid).second){for(const auto& field:types[oid]["properties"])if(field["name"]==name)return field["oid"];oid=string_value(types[oid],"superclass_oid");}return {};}
    word metadata(const std::string& full,word original,const std::array<word,10>& a){
        if(full.starts_with("?createInst@@")){auto oid=hex(reinterpret_cast<void*>(a[0]),12)+std::string(20,'0');if(memory_classes.contains(oid))return memory_classes.at(oid);return invoke<word>(original,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]);}
        auto end=full.find('@');auto name=full.substr(1,end-1),oid=oid_of(a[0]);auto type=types.find(oid);auto schema=schemas.find(oid);auto record=member(oid);
        if(context.request.value("traceNative",false))std::cerr<<full<<" oid="<<oid<<"\n";
        if(name=="values"&&metadata_collections.contains(oid)){collection(a[1],metadata_collections.at(oid));return 0;}
        if(name=="getAtKey"&&metadata_collections.contains(oid)){auto requested=utf8(reinterpret_cast<wchar_t*>(a[1]));std::string target;
            for(const auto& id:metadata_collections.at(oid))if(methods.at(id)["name"]==requested){target=id;break;}put_oid(a[2],target);return 0;}
        auto write_oid=[&](word output,const std::string& target){if(!output)throw std::runtime_error("Null output for "+full);put_oid(output,target);return word(0);};
        auto flag=[&](bool value){at<unsigned char>(a[1])=static_cast<unsigned char>(value);return word(0);};
        if(name.starts_with("is")&&type!=types.end()){
            auto category=type->second.value("category",type->second.value("primitive",false)?6:2);
            if(name=="isPrimitive")return type->second.value("primitive",false);
            if(name=="isClass")return category==2||category==3||category==4||category==5||category==54||category==66||category==68||category==69||category==209||category==210||category==226;
            if(name=="isCollClass")return category==3||category==210;
            if(name=="isPseudoType")return category==7;if(name=="isJadeInterface")return category==8;
            if(name=="isImportedClass"||name=="isImportedJadeInterface")return 0;
        }
        if(name=="getSourceSize"){auto text=record?record->value("source",std::string{}):methods.at(selected).value("source",std::string{});at<uint32_t>(a[1])=uint32_t((wide(text).size()+1)*2);return 0;}
        if(name=="getSource"){put_text(a[1],record?record->value("source",std::string{}):methods.at(selected).value("source",std::string{}));return 0;}
        if(name=="getEncryptedSourceSize"){at<uint32_t>(a[1])=0;return 0;}
        if(name=="getPrimaryLocale")return write_oid(a[1],{});
        if(name=="getModifiedTimeStamp"){std::memset(reinterpret_cast<void*>(a[1]),0,8);return 0;}
        if(name=="makeLatestVersionIfAny")return 0;
        if(name=="getNextVersionObject"||name=="getPriorVersionObject")return write_oid(a[1],{});
        if(name=="isLatestVersion")return flag(true);
        if(name=="getRootType"&&type!=types.end()){auto root=string_value(type->second,"root_type_oid");return write_oid(a[1],root.empty()?oid:root);}
        if(name=="keys"&&type!=types.end()){std::vector<std::string> ids;for(const auto& key:type->second.value("keys",json::array()))ids.push_back(key["oid"]);collection(a[1],ids);return 0;}
        if(name=="getKeysIsEmpty"&&type!=types.end())return flag(type->second.value("keys",json::array()).empty());
        if(keys.contains(oid)){if(name=="getKeyProperty")return write_oid(a[1],string_value(*record,"property_oid"));if(name=="getDescending"||name=="getCaseInsensitive")return flag(record->value(name=="getDescending"?"descending":"case_insensitive",false));if(name=="getSortOrder"){at<int32_t>(a[1])=record->value("sort_order",0);return 0;}}
        if(name=="getMemberType"&&type!=types.end()){auto target=string_value(type->second,"member_type_oid");if(target.empty())throw std::runtime_error("Collection member metadata is missing; refresh the native system cache");if(full.find("AEAPEAV")!=std::string::npos){at<word>(a[1])=type_wrapper(target);return 0;}return write_oid(a[1],target);}
        if(type!=types.end()&&(name=="getMemberTypeNumber"||name=="getMemberTypeSize"||name=="getMemberTypePrecision"||name=="getMemberTypeSFactor")){auto target=string_value(type->second,"member_type_oid");if(target.empty())throw std::runtime_error("Collection member metadata is missing; refresh the native system cache");if(name=="getMemberTypeNumber")at<uint32_t>(a[1])=types.at(target)["number"];else if(name=="getMemberTypeSize")at<uint32_t>(a[1])=type->second.value("member_type_size",0u);else at<uint8_t>(a[1])=type->second.value(name=="getMemberTypePrecision"?"member_type_precision":"member_type_scale",uint8_t(0));return 0;}
        if(name=="getOriginalClass"||name=="getOriginalInterface")return write_oid(a[1],oid);
        if((name=="getType"||name=="getTypeLatestVersion"||name=="getSchemaClass"||name=="getSchemaClassLatestVersion")&&full.find("@DskSchema@@")!=std::string::npos){auto requested=utf8(reinterpret_cast<wchar_t*>(a[1]));auto namespace_name=schemas.at(oid)["name"].get<std::string>();auto found=visible[namespace_name].find(requested);return write_oid(a[2],found==visible[namespace_name].end()?std::string{}:found->second);}
        if(schema!=schemas.end()&&(name=="getPrimitiveLatestVersion"||name=="getSchemaJadeInterfaceLatestVersion"||name=="getImportedClassLatestVersion"||name=="getImportedInterfaceLatestVersion"||name=="getImportedPackageLatestVersion")){auto requested=utf8(reinterpret_cast<wchar_t*>(a[1]));auto& scope=visible[schema->second["name"]];auto found=scope.find(requested);std::string target;if(found!=scope.end()){const auto& row=types.at(found->second);auto category=row.value("category",2);if((name=="getPrimitiveLatestVersion"&&row.value("primitive",false))||(name=="getSchemaJadeInterfaceLatestVersion"&&category==8)||(name=="getImportedClassLatestVersion"&&(category==209||category==210))||(name=="getImportedInterfaceLatestVersion"&&category==226))target=found->second;}return write_oid(a[2],target);}
        if(name=="getClassByNumber"||name=="getClassByNumberLatest"){for(const auto& [id,value]:types)if(value.value("category",2)!=7&&value.value("category",2)!=8&&!value.value("primitive",false)&&value["number"]==a[1])return write_oid(a[2],id);return write_oid(a[2],{});}
        if(name=="getPrimitiveMethodInSchema"&&type!=types.end())return write_oid(a[4],find_method(oid,utf8(reinterpret_cast<wchar_t*>(a[1]))));
        if(name.find("Method")!=std::string::npos&&name.starts_with("get")&&a[1]&&type!=types.end()&&(name=="getMethod"||name=="getLatestMethod"||name=="getMethodInHTree"||name=="getLatestMethodInHTree"||name=="getClassMethodInSchema"))return write_oid(a[name=="getClassMethodInSchema"?4:full.find("AEBVDskSchema")!=std::string::npos?3:2],find_method(oid,utf8(reinterpret_cast<wchar_t*>(a[1]))));
        if((name.find("Property")!=std::string::npos)&&(name.find("ByName")!=std::string::npos||name.find("InHTree")!=std::string::npos||name.find("InHTree")!=std::string::npos)&&type!=types.end()){auto target=find_property(oid,utf8(reinterpret_cast<wchar_t*>(a[1])));if(!target.empty()){auto kind=types.at(properties.at(target)["type_oid"]).value("primitive",false)?"DskPrimAttribute":"DskReference";invoke<word>(exported(jom,(std::string("??0")+kind+"@@QEAA@XZ").c_str()),a[2]);}return write_oid(a[2],target);}
        if(name=="getName"){if(type!=types.end())put_text(a[1],type->second["name"]);else if(record)put_text(a[1],record->value("name",std::string{}));else if(schema!=schemas.end())put_text(a[1],schema->second["name"]);else throw std::runtime_error("Name missing: "+oid);return 0;}
        if(name=="getNumber"){uint32_t number=type!=types.end()?type->second["number"].get<uint32_t>():record?record->value("number",0u):schema!=schemas.end()?schema->second["number"].get<uint32_t>():0;if(full.find("PEAG")!=std::string::npos)at<uint16_t>(a[1])=uint16_t(number);else at<uint32_t>(a[1])=number;return 0;}
        if(name=="getSuperclass"&&type!=types.end())return write_oid(a[1],string_value(type->second,"superclass_oid"));
        if(name=="childLatestSubclassesAndSubschemaSubclasses"&&type!=types.end()){std::vector<std::string> children;for(const auto& [id,value]:types)if(string_value(value,"superclass_oid")==oid)children.push_back(id);collection(a[1],children);return 0;}
        if((name=="allSuperclasses"||name=="allLatestSuperclasses"||name=="withAllSuperclasses")&&type!=types.end()){std::vector<std::string> parents;if(name=="withAllSuperclasses")parents.push_back(oid);auto parent=string_value(type->second,"superclass_oid");while(!parent.empty()){if(std::find(parents.begin(),parents.end(),parent)!=parents.end())throw std::runtime_error("Cyclic class inheritance");parents.push_back(parent);parent=string_value(types.at(parent),"superclass_oid");}collection(a[1],parents);return 0;}
        if(name=="getSchema"&&type!=types.end())return write_oid(a[1],type->second["schema_oid"]);
        if(name=="getSchema"&&record){auto owner=string_value(*record,"owner_oid"),scope=string_value(*record,"schema_oid");if(scope.empty()&&types.contains(owner))scope=string_value(types.at(owner),"schema_oid");return write_oid(a[1],scope);}
        if(name=="getClass"){auto bytes=oid_bytes(oid);auto number=at<uint32_t>(reinterpret_cast<word>(bytes.data()),8)%1000000;for(const auto& [id,value]:types)if(value.value("category",2)!=7&&value.value("category",2)!=8&&!value.value("primitive",false)&&value["number"]==number)return write_oid(a[1],id);}
        if(schema!=schemas.end()){
            if(name=="getProperty"&&hex(reinterpret_cast<void*>(a[1]),8)=="0100000039000200"){at<uint8_t>(a[2])=0;return 0;}
            if(name=="getAppClass"||name=="getGlobalClass"||name=="getWebClass"){auto key=name=="getAppClass"?"app_class_oid":name=="getGlobalClass"?"global_class_oid":"web_class_oid";return write_oid(a[1],string_value(schema->second,key));}
        }
        if(name=="getLocalPrimitiveTypeName"){for(const auto& [id,value]:types)if(value.value("primitive",false)&&value["number"]==a[1]){put_text(a[2],value["name"]);return 0;}}
        if(name=="isSystemObject")return flag(type!=types.end()?!type->second.value("source_owned",false):record?!record->value("source_owned",false):false);
        if((name=="getConstantLatestVersion"||name=="getConstant"||name=="getLocalConstant"||name=="getConstantInHTree"||name=="getLatestConstantInHTree"||name=="getConstantInSchema")&&type!=types.end())return write_oid(a[name=="getLatestConstantInHTree"||name=="getConstantInSchema"?3:2],find_constant(oid,utf8(reinterpret_cast<wchar_t*>(a[1]))));
        if((name=="getConstantLatestVersion"||name=="getConstant")&&schema!=schemas.end())return write_oid(a[2],find_global_constant(oid,utf8(reinterpret_cast<wchar_t*>(a[1]))));
        if(name=="getConstantLatestVersion")return write_oid(a[2],{});
        if(name=="getProperty"&&type!=types.end()) {auto feature=hex(reinterpret_cast<void*>(a[1]),8);auto found=native_features.find(feature);
            if(found!=native_features.end())return write_oid(a[2],string_value(type->second,found->second.c_str()));
            if(feature=="350000000f000300"&&memory_classes.contains(oid)){
                auto dictionary=identity(118,0x7b000000+metadata_collections.size());std::vector<std::string> ids;
                for(const auto& method:type->second["methods"])ids.push_back(method["oid"]);metadata_collections[dictionary]=ids;
                return write_oid(a[2],dictionary);
            }
            throw std::runtime_error("Unknown inline metadata feature: "+feature+" owner="+type->second["name"].get<std::string>());
        }
        if(name=="getPrimitiveConstantInSchema"&&type!=types.end())return write_oid(a[4],find_constant(oid,utf8(reinterpret_cast<wchar_t*>(a[1]))));
        if(name=="createObject"){auto bytes=oid_bytes(oid);auto number=at<uint32_t>(reinterpret_cast<word>(bytes.data()),8);if((number>=28&&number<=32)||number==227){auto new_oid=identity(number,0x78000000+usages.size());usages[new_oid]={{"oid",new_oid},{"type_oid",nullptr},{"length",0}};return write_oid(a[0],new_oid);}}
        if(name=="valuesFromProperty"&&methods.contains(oid)){std::vector<std::string> ids;for(const auto& p:methods.at(oid)["parameters"])ids.push_back(p["oid"]);collection(a[2],ids);return 0;}
        if(record){
            if(name=="constantUsages"&&constants.contains(oid)){std::vector<std::string> ids(constant_usages[oid].begin(),constant_usages[oid].end());collection(a[1],ids);return 0;}
            if(name=="addConstantUsage"&&constants.contains(oid)){constant_usages[oid].insert(oid_of(a[1]));return 0;}
            if(name=="clearConstantUsages"&&constants.contains(oid)){constant_usages[oid].clear();return 0;}
            if(name=="setValue"&&constants.contains(oid)){(*record)["value"]=read_value(a[1]);return 0;}
            if(name=="getValue"&&constants.contains(oid)){if(record->contains("value_unavailable"))throw std::runtime_error("Cached constant "+record->at("name").get<std::string>()+": "+record->at("value_unavailable").get<std::string>());if(record->contains("value"))write_value(a[1],record->at("value"));else at<uint16_t>(a[1])=DSKNULL;return 0;}
            if(name=="getValueLength"&&constants.contains(oid)){auto& value=record->at("value");at<uint32_t>(a[1])=record->value("value_length",value.is_string()?uint32_t(wide(value.get<std::string>()).size()*2):value.is_boolean()?1u:4u);return 0;}
            if(name=="getPrecision"||name=="getScaleFactor"||name=="getSFactor"){at<uint8_t>(a[1])=record->value(name=="getPrecision"?"precision":"scaleFactor",uint8_t(0));if(context.request.value("traceNative",false))std::cerr<<"Metadata value "<<name<<"="<<int(at<uint8_t>(a[1]))<<" record="<<*record<<"\n";return 0;}
            if(name=="implementors"&&methods.contains(oid)){
                std::vector<std::string> overrides;auto owner=record->at("owner_oid").get<std::string>();
                for(const auto& [candidate,method]:methods){if(candidate==oid||method["name"]!=record->at("name"))continue;
                    auto current=string_value(types.at(method["owner_oid"]),"superclass_oid");std::set<std::string> seen;
                    while(types.contains(current)&&seen.insert(current).second){if(current==owner){overrides.push_back(candidate);break;}current=string_value(types.at(current),"superclass_oid");}
                }collection(a[1],overrides);return 0;
            }
            if(name=="getType") {auto target=string_value(*record,"type_oid");if(full.find("AEAPEAV")!=std::string::npos){at<word>(a[1])=target.empty()?0:type_wrapper(target);return 0;}return write_oid(a[1],target);}
            if(name=="getSchemaType"){auto owner=string_value(*record,"owner_oid");if(full.find("AEAPEAV")!=std::string::npos){at<word>(a[1])=0;if(!owner.empty())at<word>(a[1])=type_wrapper(owner);return 0;}return write_oid(a[1],owner);}
            if(name=="getReturnType"){auto result=record->value("return_type",json());return write_oid(a[1],result.is_null()?std::string{}:result["oid"].get<std::string>());}
            if(name=="parameters"||name=="checkedOutMethods"){std::vector<std::string> ids;if(name=="parameters")for(const auto& p:(*record)["parameters"])ids.push_back(p["oid"]);collection(a[1],ids);return 0;}
            if(name=="getOriginalMethod"||name=="getSuperMethod")return write_oid(a[1],string_value(*record,name=="getOriginalMethod"?"original_method_oid":"super_method_oid"));
            if(name=="getLength"){at<uint32_t>(a[1])=record->value("length",0u);if(context.request.value("traceNative",false))std::cerr<<"Metadata value length="<<at<uint32_t>(a[1])<<"\n";return 0;}
            if(name=="getOrdinal"){at<uint16_t>(a[1])=uint16_t(record->value("ordinal",0));return 0;}
            if(name=="getUsage"){at<unsigned char>(a[1])=static_cast<unsigned char>(record->value("usage",0));return 0;}
            if(name=="setType"||name=="setFeature"){(*record)[name=="setType"?"type_oid":"feature_oid"]=oid_of(a[1]);return 0;}
            if(name=="setName"){(*record)["name"]=utf8(reinterpret_cast<wchar_t*>(a[1]));return 0;}
            if(name=="setReturnType"){(*record)["return_type"]=a[1]?usages.at(oid_of(a[1])):json();return 0;}
            if(name=="setUsage"||name=="setLength"||name=="setAssignment"){(*record)[name=="setUsage"?"usage":name=="setLength"?"length":"assignment"]=name=="setLength"?uint32_t(a[1]):uint8_t(a[1]);return 0;}
            if(name=="save"){invoke<void>(exported(interpreter,"?incNodeRefCount@@YAXPEAVNode@@@Z"),a[1]);roots[oid]=a[1];return 0;}
            if(name=="sendMsg"&&utf8(reinterpret_cast<wchar_t*>(a[1]))=="_moveParamsForCompile")return 0;
            if(name=="setError"){(*record)["error_code"]=a[1];(*record)["error_position"]=a[2];(*record)["error_length"]=a[3];return 0;}
            if(name.starts_with("clear")||name.starts_with("add")||name=="exportedMethodRefs"||name=="interfaceImplements"||name=="getDeprecatedDescription"||name.starts_with("setDeprecated"))return 0;
            if(name=="getControlMethod"||name=="getSchemaScript")return write_oid(a[1],name=="getControlMethod"?std::string{}:selected);
            static const std::map<std::string,std::string> scalars={{"TextPosition","text_position"},{"TextLength","text_length"},{"Status","status"},{"ErrorCode","error_code"},{"ErrorPosition","error_position"},{"ErrorLength","error_length"}};
            auto suffix=name.size()>3?name.substr(3):std::string{};auto scalar=scalars.find(suffix);
            if(scalar!=scalars.end()){if(name.starts_with("get"))at<uint32_t>(a[1])=record->value(scalar->second,0u);else(*record)[scalar->second]=a[1];return 0;}
        }
        static const std::map<std::string,std::string> flags={{"Access","access"},{"SubAccess","subaccess"},{"Abstract","abstract"},{"Final","final"},{"Updating","updating"},{"Condition","condition"},{"ConditionSafe","condition_safe"},{"NotImplemented","not_implemented"},{"LockReceiver","lock_receiver"},{"PartitionMethod","partition_method"},{"SubschemaFinal","subschema_final"},{"SubschemaCopyFinal","subschema_copy_final"},{"Options_IsTypeMethod","type_method"},{"Options_IsReceiverByReference","receiver_by_reference"},{"MethodInvocation","invocation"},{"TransientAllowed","transient_allowed"},{"PersistentAllowed","persistent_allowed"},{"SharedTransientAllowed","shared_transient_allowed"}};
        auto suffix=name.size()>3?name.substr(3):std::string{};
        if(auto found=flags.find(suffix);found!=flags.end()) {auto row=record?record:type!=types.end()?&type->second:nullptr;if(!row)throw std::runtime_error("Missing flag owner "+oid);
            const bool enumeration=found->second=="access"||found->second=="subaccess"||found->second=="invocation";
            if(name.starts_with("set")){(*row)[found->second]=enumeration?json(uint8_t(a[1])):json(bool(a[1]));return 0;}if(enumeration){at<uint8_t>(a[1])=row->value(found->second,uint8_t(0));return 0;}return flag(row->value(found->second,false));}
        if(name=="getVirtual"||name=="getRequired"||name=="getEmbedded"||name=="getKey"||name=="getDeprecated"||name=="getDeprecated_DeprecatedFlag"||name=="getAllowTransToPersistRef"||name=="getInverseNotRequired")return flag(false);
        if(name=="getUpdateMode"||name=="getKind"||name=="getUnitTestFlags"){at<uint32_t>(a[1])=0;return 0;}
        throw std::runtime_error("Unsupported metadata operation: "+full+" oid="+oid);
    }
    word safe_metadata(const std::string& name,word original,const std::array<word,10>& args){try{return metadata(name,original,args);}catch(const std::exception& error){errors.push_back(error.what());std::cerr<<error.what()<<"\n";return 1219;}}
    void hook_metadata(){
        std::map<word,word> replacements;
        {auto original=exported(jom,"jomGetProperty");auto callback=metadata_hooks.callback("jomGetProperty",original,[this](const std::array<word,10>& a){try{return metadata_property(a);}catch(const std::exception& error){errors.push_back(error.what());std::cerr<<error.what()<<"\n";return word(1219);}});metadata_hooks.replace_entry(original,callback);}
        for(auto module:{jom,compiler,interpreter,primitive_library})for(const auto& item:imports(module))if(item.name.find("josAssertFail")!=std::string::npos){auto callback=metadata_hooks.callback(item.name,item.original,[this](const std::array<word,10>& a){std::string message="Native assertion";for(size_t i=0;i<4;i++)if(a[i])message+=" | "+utf8(reinterpret_cast<wchar_t*>(a[i]));message+=" line="+std::to_string(a[4]);errors.push_back(message);std::cerr<<message<<"\n";
                if(context.request.value("traceNative",false)){void* backtrace[16]{};auto count=native_backtrace(0,16,backtrace,nullptr);for(USHORT i=0;i<count;i++){HMODULE owner{};wchar_t path[MAX_PATH]{};if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(backtrace[i]),&owner)){GetModuleFileNameW(owner,path,MAX_PATH);std::cerr<<"Assert frame "<<filename(fs::path(path).filename())<<" +0x"<<std::hex<<(reinterpret_cast<word>(backtrace[i])-reinterpret_cast<word>(owner))<<std::dec<<"\n";}}}return word(0);});metadata_hooks.replace(item.slot,callback);}
        for(const auto& item:imports(compiler)) {
            if(item.name.starts_with("?createPropertyNode@@")&&context.request.value("traceNative",false)){auto callback=metadata_hooks.callback(item.name,item.original,[item](const std::array<word,10>& a){std::cerr<<"Create property node type="<<a[2]<<" length/precision="<<a[3]<<" scale="<<a[4]<<"\n";return invoke<word>(item.original,a[0],a[1],a[2],a[3],a[4],a[5],a[6]);});metadata_hooks.replace(item.slot,callback);continue;}

            bool category=item.name.starts_with("?isPrimitive@")||item.name.starts_with("?isClass@")||item.name.starts_with("?isCollClass@")||item.name.starts_with("?isJadeInterface@")||item.name.starts_with("?isPseudoType@")||item.name.starts_with("?isImportedClass@")||item.name.starts_with("?isImportedJadeInterface@");
            bool method=item.name.starts_with("?")&&item.name.find("@Dsk")!=std::string::npos&&
                (item.name.find("@@UEBAH")!=std::string::npos||item.name.find("@@QEBAH")!=std::string::npos||item.name.find("@@UEAAH")!=std::string::npos||item.name.find("@@QEAAH")!=std::string::npos||category);
            if(!method&&!item.name.starts_with("?createInst@@YAPEAVDskObject@@"))continue;
            auto callback=metadata_hooks.callback(item.name,item.original,[this,item](const std::array<word,10>& args)->word{try{return metadata(item.name,item.original,args);}catch(const std::exception& error){errors.push_back(error.what());std::cerr<<error.what()<<"\n";return 1219;}});
            metadata_hooks.replace(item.slot,callback);replacements[item.original]=callback;
        }
        // Signature comparison calls this accessor only through the vtable, so
        // it has no compiler IAT entry. Leaving it unbound reads database state.
        for(auto name:{"?getPartitionMethod@DskMethod@@UEBAHPEA_N@Z","?getUpdating@DskMethod@@UEBAHPEA_N@Z","?getLockReceiver@DskMethod@@UEBAHPEA_N@Z","?getOptions_IsReceiverByReference@DskRoutine@@UEBAHPEA_N@Z","?getMethodInvocation@DskMethod@@UEBAHPEAW4MethodInvocation@@@Z"}){auto original=exported(jom,name);if(!replacements.contains(original))replacements[original]=metadata_hooks.callback(name,original,[this,name,original](const std::array<word,10>& args){return safe_metadata(name,original,args);});}
        for(auto name:{"?getScaleFactor@DskMemPrimAttribute@@UEBAHPEAE@Z","?getMemberType@DskCollClass@@UEBAHAEAVDskType@@@Z","?getMemberType@DskCollClass@@UEBAHAEAPEAVDskType@@@Z"}){auto original=exported(jom,name);if(!replacements.contains(original))replacements[original]=metadata_hooks.callback(name,original,[this,name,original](const std::array<word,10>& args){return safe_metadata(name,original,args);});}
        for(auto name:{"?getPrecision@DskPrimAttribute@@QEBAHPEAE@Z","?getScaleFactor@DskPrimAttribute@@UEBAHPEAE@Z"}){auto original=exported(jom,name);auto callback=metadata_hooks.callback(name,original,[this,name,original](const std::array<word,10>& args){return safe_metadata(name,original,args);});metadata_hooks.replace_entry(original,callback);}
        for(auto name:{"?getMemberType@DskMemCollClass@@UEBAHAEAVDskType@@@Z","?getMemberType@DskMemCollClass@@UEBAHAEAPEAVDskType@@@Z","?getMemberTypeNumber@DskMemCollClass@@UEBAHPEAH@Z"}){auto original=exported(jom,name);if(!replacements.contains(original))replacements[original]=metadata_hooks.callback(name,original,[this,name,original](const std::array<word,10>& args){return safe_metadata(name,original,args);});}
        // These predicates are often virtual-only, with no compiler IAT entry.
        // A generic DskType wrapper must still classify its cached identity.
        for(auto kind:{"DskType","DskClass","DskCollClass","DskPrimType","DskPseudoType","DskJadeInterface","DskMemPrimType","DskMemClass"})for(auto predicate:{"isPrimitive","isPseudoType","isClass","isCollClass","isJadeInterface","isImportedClass","isImportedJadeInterface"}){
            auto name=std::string("?")+predicate+"@"+kind+"@@UEBA_NXZ";auto original=reinterpret_cast<word>(GetProcAddress(jom,name.c_str()));if(!original||replacements.contains(original))continue;
            replacements[original]=metadata_hooks.callback(name,original,[this,name,original](const std::array<word,10>& args){return safe_metadata(name,original,args);});
        }
        auto inventory=exports(jom);std::vector<word> tables;for(auto& [name,address]:inventory)if(name.starts_with("??_7Dsk"))tables.push_back(address);std::sort(tables.begin(),tables.end());
        tables.erase(std::unique(tables.begin(),tables.end()),tables.end());for(size_t i=0;i<tables.size();++i){auto end=std::min(tables[i]+4096,i+1<tables.size()?tables[i+1]:tables[i]+4096);for(auto slot=tables[i];slot<end;slot+=8){auto target=at<word>(slot);MEMORY_BASIC_INFORMATION page{};if(!VirtualQuery(reinterpret_cast<void*>(target),&page,sizeof(page))||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))break;if(auto replacement=replacements.find(target);replacement!=replacements.end())metadata_hooks.replace(slot,replacement->second);}}
    }
    void prepare(){
        auto cache=system_cache(context);auto root_schema=cache.at("schema");for(const auto& constant:root_schema.value("constants",json::array()))constants[constant["oid"]]=constant;auto root_oid=root_schema["oid"].get<std::string>();schemas[root_oid]=root_schema;
        progress("system-cache","Using C++ RootSchema cache ("+std::to_string(cache["types"].size())+" system types). Workspace source supplies application definitions. No database connection.");
        for(auto& type:cache["types"]){auto oid=type["oid"].get<std::string>();types[oid]=type;visible["RootSchema"][type["name"]]=oid;}
        // Materialize MemberType signatures in each concrete collection scope.
        // Metadata comes from the refreshed cache, never from a class-name guess.
        size_t generic_serial=0;std::map<std::string,const json*> generic_types;for(const auto& type:cache["types"])generic_types[type["oid"]]=&type;
        for(auto& [oid,type]:types)if(!string_value(type,"member_type_oid").empty()){
            auto member_type=type["member_type_oid"].get<std::string>();std::vector<json> inherited;std::set<std::string> names;auto current=oid;std::set<std::string> visited;
            while(generic_types.contains(current)&&visited.insert(current).second){for(auto method:generic_types.at(current)->at("methods")){if(!names.insert(method["name"]).second)continue;bool changed=false;
                    auto replace=[&](json& usage,bool parameter){auto target=string_value(usage,"type_oid");if(types.contains(target)&&types.at(target)["name"]=="MemberType"){usage["type_oid"]=member_type;usage["oid"]=identity(parameter?31:32,0x6a000000+generic_serial++);changed=true;}};
                    for(auto& parameter:method["parameters"])replace(parameter,true);if(!method["return_type"].is_null())replace(method["return_type"],false);
                    if(changed){method["oid"]=identity(24,0x69000000+generic_serial++);inherited.push_back(std::move(method));}}
                current=string_value(*generic_types.at(current),"superclass_oid");}
            for(const auto& method:inherited){auto& members=type["methods"];bool replaced=false;for(auto& existing:members)if(existing["name"]==method["name"]){existing=method;replaced=true;break;}if(!replaced)members.push_back(method);}
        }
        index=refresh_index(context);
        // Freshness is content-validated for the dependency closure, even if a
        // writer preserves size/mtime or an editor watcher missed a change.
        for(size_t round=0;round<=index["entries"].size();++round){
            auto saved=read_json(context.index_path());std::map<std::string,const json*> entries;
            for(const auto& entry:index["entries"])entries[entry["schema"].get<std::string>()+"::"+entry["name"].get<std::string>()]=&entry;
            std::vector<std::string> queue;std::set<std::string> seen;auto selected_source=jade::canonical(filepath(context.request.at("source")));
            for(const auto& [key,row]:entries)for(const auto& file:row->value("fragments",json::array({row->at("path")})))if(lexical_key(filepath(file))==selected_source)queue.push_back(key);
            std::set<std::string> changed_files;
            for(size_t i=0;i<queue.size();++i){auto key=queue[i];if(!seen.insert(key).second)continue;if(saved["dependencies"].contains(key))for(const auto& dependency:saved["dependencies"][key])queue.push_back(dependency);
                if(!entries.contains(key))continue;for(const auto& file:entries.at(key)->value("fragments",json::array({entries.at(key)->at("path")}))){auto path=filepath(file);auto normalized=lexical_key(path);if(saved["files"].at(normalized)["hash"]!=file_hash(path))changed_files.insert(filename(path));}}
            if(changed_files.empty())break;if(round==index["entries"].size())throw std::runtime_error("Workspace did not stabilize while reading dependencies");context.request["validateFiles"]=changed_files;index=refresh_index(context);
        }
        size_t serial=0;
        for(auto& [name,parent]:index["parents"].items()) {auto schema=root_schema;auto oid=identity(1,0x72000000+serial);schema["oid"]=oid;schema["number"]=400+serial++;schema["name"]=name;schema["incomplete"]=false;schemas[oid]=schema;visible[name]=visible["RootSchema"];}
        auto selected_source=jade::canonical(filepath(context.request.at("source")));
        std::map<std::string,const json*> source_entries;for(const auto& entry:index["entries"])if(entry.value("hasHeader",false))source_entries[entry["schema"].get<std::string>()+"::"+entry["name"].get<std::string>()]=&entry;
        std::function<bool(std::string,const std::string&,std::set<std::string>)> source_collection=[&](std::string schema,const std::string& name,std::set<std::string> seen){while(!schema.empty()){auto key=schema+"::"+name;if(!seen.insert(key).second)throw std::runtime_error("Cyclic source inheritance: "+key);if(source_entries.contains(key)){auto entry=source_entries.at(key);return source_collection(entry->at("schema"),entry->at("parent"),std::move(seen));}schema=index["parents"].value(schema,std::string{});}auto found=visible["RootSchema"].find(name);return found!=visible["RootSchema"].end()&&types.at(found->second).value("category",2)==3;};
        for(const auto& entry:index["entries"]){if(!entry.value("hasHeader",false))continue;auto oid=identity(source_collection(entry["schema"],entry["parent"],{})?3:2,0x71000000+serial);auto schema=entry["schema"].get<std::string>();auto number=60001+serial++;
            auto record=entry;record["oid"]=oid;record["number"]=number;record["primitive"]=false;record["category"]=2;record["source_owned"]=true;record["abstract"]=entry.value("options",std::string{}).find("abstract")!=std::string::npos;record["final"]=false;record["access"]=0;record["subaccess"]=0;record["transient_allowed"]=true;record["persistent_allowed"]=false;record["shared_transient_allowed"]=true;
            for(const auto& [id,value]:schemas)if(value["name"]==schema)record["schema_oid"]=id;
            types[oid]=record;visible[schema][entry["name"]]=oid;
            if(lexical_key(filepath(entry["path"]))==selected_source)primary=oid;
            for(const auto& fragment:entry.value("fragments",json::array()))if(lexical_key(filepath(fragment))==selected_source)primary=oid;
        }
        // Resolve schema inheritance after every source identity is allocated.
        for(auto& [schema,map]:visible){if(schema=="RootSchema")continue;std::string parent=index["parents"].value(schema,std::string{});std::set<std::string> seen;while(!parent.empty()&&seen.insert(parent).second){for(auto& [name,oid]:visible[parent])map.try_emplace(name,oid);parent=index["parents"].value(parent,std::string{});}}
        // Collection identity and member metadata follow source inheritance.
        std::set<std::string> resolving,resolved;
        std::function<void(const std::string&)> configure=[&](const std::string& oid){if(resolved.contains(oid)||!types.at(oid).value("source_owned",false))return;if(!resolving.insert(oid).second)throw std::runtime_error("Cyclic source inheritance");auto& type=types.at(oid);auto parent=type_named(type["schema"],type["parent"]);configure(parent);const auto& base=types.at(parent);if(base.value("category",2)==3){type["category"]=3;for(auto key:{"member_type_oid","member_type_size","member_type_precision","member_type_scale","keys"})if(base.contains(key))type[key]=base[key];}resolving.erase(oid);resolved.insert(oid);};
        for(const auto& [oid,type]:types)if(type.value("source_owned",false))configure(oid);
        if(primary.empty())throw std::runtime_error("Selected source has no complete class declaration");
        std::vector<std::string> pending={primary};static const std::regex identifier(R"(\b[A-Za-z_]\w*\b)");
        while(!pending.empty()){
            auto oid=pending.back();pending.pop_back();if(!needed.insert(oid).second)continue;
            const auto& type=types.at(oid);if(!type.value("source_owned",false))continue;
            auto schema=type["schema"].get<std::string>();pending.push_back(type_named(schema,type["parent"]));
            for(const auto& field:type["properties"])pending.push_back(type_named(schema,field["type"]));
            for(const auto& constant:type.value("constants",json::array())){pending.push_back(type_named(schema,constant["type"]));auto source=constant.value("source",std::string{});for(auto at=std::sregex_iterator(source.begin(),source.end(),identifier);at!=std::sregex_iterator();++at){auto found=visible[schema].find(at->str());if(found!=visible[schema].end())pending.push_back(found->second);}}
            for(const auto& method:type["methods"]){auto source=method["source"].get<std::string>();
                for(auto at=std::sregex_iterator(source.begin(),source.end(),identifier);at!=std::sregex_iterator();++at){auto name=at->str();auto found=visible[schema].find(name);if(found!=visible[schema].end())pending.push_back(found->second);}
                auto result=method["returnType"].get<std::string>();if(!result.empty())pending.push_back(type_named(schema,result));for(const auto& p:method["parameters"])pending.push_back(type_named(schema,p["type"]));}
        }
        for(const auto& [oid,type]:types)for(const auto& key:type.value("keys",json::array()))keys[key["oid"]]=key;
        for(auto& [oid,type]:types){auto schema=type.value("schema",std::string("RootSchema"));
            if(type.value("source_owned",false)&&!needed.contains(oid))continue;
            if(type.value("source_owned",false)){type["superclass_oid"]=type_named(schema,type["parent"]);size_t mindex=0;
                for(auto& method:type["methods"]){method["oid"]=identity(23,0x74000000+serial++);method["owner_oid"]=oid;method["source_owned"]=true;method["source_path"]=method.value("source_path",type["path"].get<std::string>());auto flags=method["flags"].get<std::string>();method["updating"]=flags.find("updating")!=std::string::npos;method["abstract"]=false;method["access"]=0;method["subaccess"]=0;method["final"]=false;
                    auto has=[&](const char* name){return std::regex_search(flags,std::regex(std::string("\\b")+name+"\\b"));};
                    method["abstract"]=has("abstract");method["final"]=has("final");method["access"]=has("protected")?2:has("readOnly")?1:0;
                    method["type_method"]=has("typeMethod");method["condition"]=has("condition");method["lock_receiver"]=has("lockReceiver");
                    method["subschema_final"]=has("subschemaFinal");method["subschema_copy_final"]=has("subschemaCopyFinal");method["receiver_by_reference"]=has("receiverByReference");
                    auto result=method["returnType"].get<std::string>();method["return_type"]=result.empty()?json():json{{"oid",identity(32,0x75000000+serial++)},{"type_oid",type_named(schema,result)},{"length",0}};
                    for(auto& p:method["parameters"]){p["oid"]=identity(31,0x76000000+serial++);p["type_oid"]=type_named(schema,p["type"]);auto usage=p["usage"].get<std::string>();p["usage"]=usage=="input"?1:usage=="io"?2:usage=="output"?3:0;}++mindex;}
                for(auto& field:type["properties"]){field["type_oid"]=type_named(schema,field["type"]);field["oid"]=identity(types.at(field["type_oid"]).value("primitive",false)?12:14,0x7a000000+serial++);field["owner_oid"]=oid;}
                for(auto& constant:type["constants"]){constant["type_oid"]=type_named(schema,constant["type"]);constant["oid"]=identity(17,0x7c000000+serial++);constant["owner_oid"]=oid;constant["access"]=0;constant["subaccess"]=0;constant["status"]=constant.contains("value")?1:0;constant["source_owned"]=true;constant["length"]=constant.contains("value")&&constant["value"].is_string()?wide(constant["value"].get<std::string>()).size():0;constants[constant["oid"]]=constant;}
            }
            if(!type.value("source_owned",false)&&type.contains("constants")&&type["constants"].is_array())for(const auto& constant:type["constants"])constants[constant["oid"]]=constant;
            for(const auto& method:type["methods"]){auto id=method["oid"].get<std::string>();methods[id]=method;if(method.contains("return_type")&&!method["return_type"].is_null())usages[method["return_type"]["oid"]]=method["return_type"];for(auto p:method["parameters"])usages[p["oid"]]=p;}
            for(auto& field:type["properties"]){field["owner_oid"]=oid;auto property_type=types.find(string_value(field,"type_oid"));if(!field.contains("length")&&property_type!=types.end()&&property_type->second.value("primitive",false)&&property_type->second["name"]!="String"&&property_type->second["name"]!="StringUtf8"&&property_type->second["name"]!="Binary")field["length"]=0;properties[field["oid"]]=field;}
        }
    }
    // Graph objects belong to the native allocator. Retain a graph reference
    // for this one-job worker so compiler scopes cannot delete shared metadata.
    void retain_graph_object(word object){++at<int32_t>(object,8);}
    word make_class(const json& type){bool coll=type.value("category",2)==3;auto object=native_alloc(coll?656:600);auto name=wide(type["name"]);uint16_t max_property=0,max_ordinal=0,max_method=0;for(const auto& p:type["properties"]){max_property=std::max(max_property,uint16_t(p["number"]));max_ordinal=std::max(max_ordinal,uint16_t(p["ordinal"]));}for(const auto& m:type["methods"])max_method=std::max(max_method,uint16_t(m["number"]));
        if(coll)invoke<word>(exported(jom,"??0DskMemCollClass@@QEAA@QEB_WHGGGGGW4ObjectVolatility@@_NW4AccessType@@32PEAVDskType@@IIIIII222222222@Z"),object,name.c_str(),int(type["number"]),max_property,max_ordinal,max_method,uint16_t(0),uint16_t(0),1,false,uint8_t(0),uint8_t(0),false,type_wrapper(type["member_type_oid"]),uint32_t(0),uint32_t(0),uint32_t(0),uint32_t(0),uint32_t(0),uint32_t(0),false,false,true,true,false,true,true,false,true);
        else invoke<word>(exported(jom,"??0DskMemClass@@QEAA@QEB_WHGGGGGW4ObjectVolatility@@_NW4AccessType@@3222222222@Z"),object,name.c_str(),int(type["number"]),max_property,max_ordinal,max_method,uint16_t(0),uint16_t(0),1,false,uint8_t(0),uint8_t(0),false,true,true,true,true,false,true,true,false);
        retain_graph_object(object);put_oid(object,type["oid"]);invoke<int>(exported(jom,coll?"?setAbstract@DskMemCollClass@@UEAAH_N@Z":"?setAbstract@DskMemClass@@UEAAH_N@Z"),object,type.value("abstract",false));return object;
    }
    void graph(){
        for(const auto& [oid,schema]:schemas){auto value=native_alloc(728);auto name=wide(schema["name"]);invoke<word>(exported(jom,"??0DskMemSchema@@QEAA@QEB_W0@Z"),value,name.c_str(),L"25.0.02");retain_graph_object(value);put_oid(value,oid);memory_schemas[oid]=value;}
        for(const auto& [oid,type]:types)if(type.value("source_owned",false)&&needed.contains(oid)){memory_classes[oid]=make_class(type);auto parent=type["superclass_oid"].get<std::string>();if(!memory_classes.contains(parent))memory_classes[parent]=make_class(types.at(parent));}
        for(const auto& [oid,type]:types)if(type.value("source_owned",false)&&needed.contains(oid)){
            auto owner=memory_classes.at(oid),parent=memory_classes.at(type["superclass_oid"]),schema=memory_schemas.at(type["schema_oid"]);
            if(invoke<int>(exported(jom,"?addSuperclass@DskMemClass@@UEAAHPEBVDskClass@@@Z"),owner,parent))throw std::runtime_error("Native superclass link failed");
            invoke<int>(exported(jom,"?addClass@DskMemSchema@@UEAAHPEBVDskClass@@@Z"),schema,owner);
        }
        for(const auto& [schema_id,schema]:memory_schemas){std::map<std::string,word> primitives;
            for(const auto& [oid,type]:types)if(type.value("primitive",false)||type.value("category",2)==7){auto object=native_alloc(328);auto name=wide(type["name"]);
                invoke<word>(exported(jom,"??0DskMemPrimType@@QEAA@QEB_WHGW4AccessType@@1@Z"),object,name.c_str(),int(type["number"]),uint16_t(0),uint8_t(0),uint8_t(0));retain_graph_object(object);put_oid(object,oid);
                auto entry=type.value("category",2)==7?"?addPseudoType@DskMemSchema@@UEAAHPEBVDskPseudoType@@@Z":"?addPrimitive@DskMemSchema@@UEAAHPEBVDskPrimType@@@Z";auto status=invoke<int>(exported(jom,entry),schema,object);if(status)throw std::runtime_error("Native primitive/pseudotype registration failed");primitives[oid]=object;}
            for(const auto& [oid,type]:types)if(type.value("source_owned",false)&&needed.contains(oid)&&type["schema_oid"]==schema_id)for(const auto& field:type["properties"]){auto primitive=primitives.find(field["type_oid"]);if(primitive==primitives.end()||types.at(field["type_oid"])["name"]=="Decimal")continue;auto object=native_alloc(280);auto name=wide(field["name"]);
                invoke<word>(exported(jom,"??0DskMemPrimAttribute@@QEAA@QEB_WPEAVDskType@@1GGGIW4AccessType@@2_N3@Z"),object,name.c_str(),memory_classes.at(oid),primitive->second,uint16_t(field["number"]),uint16_t(field["ordinal"]),uint16_t(0),uint32_t(field["length"]),field.value("access",uint8_t(0)),field.value("subaccess",uint8_t(0)),false,false);retain_graph_object(object);put_oid(object,field["oid"]);
                if(invoke<int>(exported(jom,"?addProperty@DskMemClass@@UEAAHPEBVDskProperty@@@Z"),memory_classes.at(oid),object))throw std::runtime_error("Native property registration failed");}
        }
    }
public:
    static LONG WINAPI trace_native_exception(EXCEPTION_POINTERS* exception){if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){HMODULE module{};char path[MAX_PATH]{};auto address=exception->ExceptionRecord->ExceptionAddress;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module);GetModuleFileNameA(module,path,MAX_PATH);std::fprintf(stderr,"Native first-chance fault in %s +0x%llx rcx=0x%llx rdx=0x%llx\n",path,static_cast<unsigned long long>(reinterpret_cast<word>(address)-reinterpret_cast<word>(module)),exception->ContextRecord->Rcx,exception->ContextRecord->Rdx);void* frames[32]{};auto count=native_backtrace_from(*exception->ContextRecord,0,32,frames);for(USHORT i=0;i<count;i++){HMODULE owner{};char name[MAX_PATH]{};if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[i]),&owner)){GetModuleFileNameA(owner,name,MAX_PATH);std::fprintf(stderr,"Fault frame %s +0x%llx\n",name,static_cast<unsigned long long>(reinterpret_cast<word>(frames[i])-reinterpret_cast<word>(owner)));}}}return EXCEPTION_CONTINUE_SEARCH;}
    void* exception_trace{};
    ~NativeProgram(){if(exception_trace)RemoveVectoredExceptionHandler(exception_trace);}
    explicit NativeProgram(Context& value):context(value){
        static const std::map<std::string,std::string> pins={{"jomutil.dll","0d18d3ad8fa7fc5e72e1106e1a9817089280328f2569e56c0d058fef0b4d00f2"},{"jomsupp.dll","3916cbd27669e6f4b21efd90b22cd4700229afa33cc868e87e07eb920f90e7ac"},{"jom.dll","e2fb2ffef73df3c6af6ebc7a226f3ea9544660fced9e3c9c3fe28af09d088b33"},{"jlncomp.dll","cef9c02e78ef978d2400c64558c4199029ba0140b17b5a5bc65bec7cd428962c"},{"jlnintp.dll","24b922978b7914ba69308e1e323a82ff0f8c6b0b2283664b6211702273dd2c23"}};
        for(auto& [name,digest]:pins)if(file_hash(context.install/L"bin"/name)!=digest)throw std::runtime_error("Unsupported native DLL build: "+name);
        auto directory=context.install/L"bin";SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);AddDllDirectory(directory.c_str());
        auto load=[&](const wchar_t* name){auto module=LoadLibraryExW((directory/name).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module)throw std::runtime_error("Cannot load Jade runtime library");return module;};
        jom=load(L"jom.dll");compiler=load(L"jlncomp.dll");load(L"jlnctab.dll");interpreter=load(L"jlnintp.dll");messages=load(L"jomutil.dll");primitive_library=load(L"jomsupp.dll");comp=reinterpret_cast<word>(compiler);interp=reinterpret_cast<word>(interpreter);
        for(const auto& [name,key]:std::map<std::string,std::string>{{"?getSuperclass@DskClass@@QEBAHAEAV1@@Z","superclass_oid"},{"?getSchema@DskType@@UEBAHAEAVDskSchema@@@Z","schema_oid"}}){auto function=exported(jom,name.c_str());for(size_t i=0;i<32;++i)if(at<uint8_t>(function,i)==0x48&&at<uint8_t>(function,i+1)==0x8d&&at<uint8_t>(function,i+2)==0x15){auto address=function+i+7+at<int32_t>(function,i+3);native_features[hex(reinterpret_cast<void*>(address),8)]=key;break;}}
        prepare();access=std::make_unique<AccessGuard>(context.install,context.request.value("traceNative",false));graph();hook_metadata();
    }
    json compile(){
        if(context.request.value("traceNative",false))exception_trace=AddVectoredExceptionHandler(1,trace_native_exception);
        auto status=invoke<int>(comp+0x56cc0);if(status)throw std::runtime_error("Grammar initialization failed: "+std::to_string(status));size_t count=0;
        for(auto& [oid,constant]:constants){if(!constant.value("source_owned",false)||constant.contains("value"))continue;selected=oid;auto native_constant=wrap("DskConstant",oid);auto compile_context=allocate(3224),descriptor=allocate(248);invoke<word>(comp+0x6d190,compile_context);invoke<word>(comp+0x229f0,descriptor);
            progress("compiling","Compiling constant "+types.at(constant["owner_oid"])["name"].get<std::string>()+"::"+constant["name"].get<std::string>()+" ...");with_execution_context([&]()->json{invoke<void>(comp+0x57cd0,compile_context,native_constant,descriptor,false,false);return nullptr;});auto code=at<int>(descriptor);if(code){auto position=std::max(0,at<int>(descriptor,4));auto text=wide(constant["source"]);auto prefix=text.substr(0,std::min(text.size(),size_t(position)));auto newline=prefix.rfind(L'\n');diagnostics.push_back({{"code",code},{"source",constant["source_path"]},{"owner",types.at(constant["owner_oid"])["name"]},{"method",constant["name"]},{"kind","constant"},{"line",constant["line"].get<int>()+int(std::count(prefix.begin(),prefix.end(),L'\n'))},{"column",newline==std::wstring::npos?prefix.size()+1:prefix.size()-newline}});}if(!errors.empty())break;
        }
        bool constant_failed=!diagnostics.empty()||!errors.empty();for(auto& [oid,method]:methods){if(constant_failed)break;if(!method.value("source_owned",false)||method.value("source",std::string{}).empty())continue;selected=oid;auto native_method=wrap("DskJadeMethod",oid);auto owner=memory_classes.at(method["owner_oid"]);auto schema=memory_schemas.at(types.at(method["owner_oid"])["schema_oid"]);
            progress("compiling","Compiling "+types.at(method["owner_oid"])["name"].get<std::string>()+"::"+method["name"].get<std::string>()+" in "+method["source_path"].get<std::string>()+" ...");
            auto compile_context=allocate(3224),descriptor=allocate(248);invoke<word>(comp+0x6d190,compile_context);invoke<word>(comp+0x229f0,descriptor);bool changed=false;
            invoke<void>(comp+0x56fb0,compile_context,native_method,owner,schema,&changed,descriptor,false,false);++count;
            auto code=at<int>(descriptor);if(code){auto position=std::max(0,at<int>(descriptor,4));auto source=wide(method["source"]);auto prefix=source.substr(0,std::min(source.size(),size_t(position)));auto line=method["line"].get<int>()+int(std::count(prefix.begin(),prefix.end(),L'\n'));auto newline=prefix.rfind(L'\n');auto column=newline==std::wstring::npos?prefix.size()+1:prefix.size()-newline;
                json diagnostic={{"code",code},{"source",method["source_path"]},{"owner",types.at(method["owner_oid"])["name"]},{"method",method["name"]},{"line",line},{"column",column}};
                diagnostics.push_back(diagnostic);}
            else if(!roots.contains(oid))throw std::runtime_error("Native compiler did not retain an executable method tree");
            else progress("compiled","Compile complete: "+method["name"].get<std::string>());
        }
        if(!errors.empty())return {{"success",false},{"error","Unresolved native metadata: "+errors.dump()},{"failureKind","metadata"},{"metadataErrors",errors},{"methodCount",count},{"diagnostics",diagnostics}};
        if(!access->attempts.empty())throw std::runtime_error("Native access was blocked: "+access->attempts.dump());
        return {{"success",diagnostics.empty()},{"methodCount",count},{"diagnostics",diagnostics},{"nativeCompiler",true},{"databaseNeverOpened",true},{"pythonProcessesStarted",0},{"accessAttempts",access->attempts},{"guardedEntries",access->entries}};
    }
    void execution_ready(){if(context.request.value("traceNative",false))std::cerr<<"Preparing native execution\n";for(const auto& [oid,root]:roots)templates[oid]=save_tree(root);hook_services();if(context.request.value("traceNative",false))std::cerr<<"Prepared native execution\n";}
    json execute(){execution_ready();auto method=find_method(primary,context.request.value("method",std::string("answer")));if(!roots.contains(method))throw std::runtime_error("No native executable method selected");auto& type=types.at(primary);if(type.value("abstract",false))throw std::runtime_error("Cannot instantiate an abstract class");
        auto oid=identity(type["number"].get<uint32_t>()|0x400000,0x6ffff001);auto receiver=create_receiver(oid);frames.emplace_back(method,oid);
        json value;std::exception_ptr pending;try{lifecycle(oid,true);if(failures.empty()&&service_errors.empty())value=evaluate(roots.at(method),receiver,context.request.value("arguments",json::array()),method);}catch(...){pending=std::current_exception();}try{lifecycle(oid,false);}catch(...){if(!pending)pending=std::current_exception();}frames.pop_back();if(pending)std::rethrow_exception(pending);
        if(!access->attempts.empty())throw std::runtime_error("Native access was blocked");if(!service_errors.empty())throw std::runtime_error("Offline runtime service failure: "+service_errors.dump());if(!errors.empty())throw std::runtime_error("Native runtime metadata/assertion failure: "+errors.dump());
        json fields=json::array();for(const auto& [key,data]:values)fields.push_back({{"receiver",key.first},{"feature",key.second},{"value",read_value(data.address())}});
        return {{"success",failures.empty()},{"value",value},{"nativeInterpreter",true},{"databaseNeverOpened",true},{"accessAttempts",access->attempts},{"pythonProcessesStarted",0},{"failures",failures},{"assertions",assertions.size()},{"localProperties",fields}};
    }
    json run_tests(){
        execution_ready();auto& owner=types.at(primary);if(owner.value("abstract",false))throw std::runtime_error("Select a concrete test class");
        std::map<std::string,std::string> effective,hooks;std::vector<std::string> order;auto cursor=primary;bool test_case=false;
        const std::vector<std::string> roles={"unitTestBefore","unitTestAfter","unitTestBeforeClass","unitTestAfterClass","unitTestBeforeAll","unitTestAfterAll"};
        while(types.contains(cursor)){auto& type=types.at(cursor);test_case=test_case||type["name"]=="JadeTestCase";std::set<std::string> declared_roles;
            for(const auto& declaration:type["methods"]){auto id=declaration["oid"].get<std::string>();auto& method=methods.at(id);auto name=method["name"].get<std::string>();if(!effective.contains(name)){effective[name]=id;order.push_back(name);}
                auto flags=method.value("flags",std::string{});for(const auto& role:roles)if(std::regex_search(flags,std::regex("\\b"+role+"\\b"))){if(!declared_roles.insert(role).second)throw std::runtime_error("Duplicate lifecycle role: "+role);if(role.ends_with("All")&&type["name"]!="JadeTestCase")continue;hooks.try_emplace(role,id);}}
            cursor=string_value(type,"superclass_oid");
        }
        if(!test_case)throw std::runtime_error("Selected class must inherit JadeTestCase");
        std::vector<std::string> selected_methods;
        if(context.request.contains("methods"))selected_methods=context.request["methods"].get<std::vector<std::string>>();
        else for(const auto& name:order)if(std::regex_search(methods.at(effective.at(name)).value("flags",std::string{}),std::regex(R"(\b(unitTest|unitTestIgnore)\b)")))selected_methods.push_back(name);
        if(selected_methods.empty())throw std::runtime_error("No unitTest methods selected");
        std::map<std::string,json> issues;std::map<std::string,std::string> statuses;std::vector<std::string> runnable;
        for(const auto& name:selected_methods){if(!effective.contains(name)||!std::regex_search(methods.at(effective.at(name)).value("flags",std::string{}),std::regex(R"(\b(unitTest|unitTestIgnore)\b)")))throw std::runtime_error("Invalid selected test: "+name);issues[name]=json::array();statuses[name]="skipped";if(methods.at(effective.at(name)).value("flags",std::string{}).find("unitTestIgnore")==std::string::npos)runnable.push_back(name);}
        auto oid=identity(owner["number"].get<uint32_t>()|0x400000,0x6ffff001);auto receiver=create_receiver(oid);
        auto phase=[&](const std::string& method,const std::string& role)->json {
            if(method.empty())return json::array();auto& metadata=methods.at(method);if(!roots.contains(method))throw std::runtime_error("No executable test/lifecycle source: "+metadata["name"].get<std::string>());
            if(!metadata["parameters"].empty()||!metadata["return_type"].is_null())throw std::runtime_error("Test/lifecycle method must have no parameters or return value");
            expected_exceptions.clear();matched_exceptions=0;
            auto start=failures.size(),error_start=service_errors.size();frames.emplace_back(method,oid);try{evaluate(roots.at(method),receiver,json::array(),method);}catch(...){frames.pop_back();throw;}frames.pop_back();
            if(!expected_exceptions.empty()&&matched_exceptions==0&&failures.size()==start)failures.push_back({{"operation","expectedException"},{"message","Expected Jade exception was not raised"},{"values",json::array()}});expected_exceptions.clear();
            if(!errors.empty())throw std::runtime_error("Native runtime metadata/assertion failure: "+errors.dump());
            if(error_start!=service_errors.size())throw std::runtime_error("Offline runtime service failure: "+service_errors.dump());json result=json::array();for(size_t i=start;i<failures.size();++i){auto value=failures[i];value["phase"]=role;result.push_back(value);}return result;
        };
        auto lifecycle_phase=[&](bool creating)->json{auto start=failures.size(),errors_before=service_errors.size();expected_exceptions.clear();matched_exceptions=0;lifecycle(oid,creating);if(service_errors.size()!=errors_before)throw std::runtime_error("Offline lifecycle service failure: "+service_errors.dump());if(!errors.empty())throw std::runtime_error("Native lifecycle metadata failure: "+errors.dump());json result=json::array();for(size_t i=start;i<failures.size();i++){auto issue=failures[i];issue["phase"]=creating?"constructor":"destructor";result.push_back(issue);}return result;};
        auto hook=[&](const std::string& role){return phase(hooks.contains(role)?hooks.at(role):std::string{},role);};
        auto append=[&](const std::string& name,const json& errors){for(const auto& error:errors)issues[name].push_back(error);if(!errors.empty())statuses[name]="failed";};
        std::exception_ptr pending;
        if(!runnable.empty()){
            try{auto setup=lifecycle_phase(true);if(setup.empty())setup=hook("unitTestBeforeAll");if(setup.empty())setup=hook("unitTestBeforeClass");
                if(!setup.empty())append(runnable.front(),setup);
                else for(const auto& name:runnable){statuses[name]="passed";std::exception_ptr error;
                    try{auto setup_test=hook("unitTestBefore");append(name,setup_test);if(setup_test.empty())append(name,phase(effective.at(name),"unitTest"));}catch(...){error=std::current_exception();}
                    try{append(name,hook("unitTestAfter"));}catch(...){if(!error)error=std::current_exception();}if(error)std::rethrow_exception(error);}
            }catch(...){pending=std::current_exception();}
            try{append(runnable.back(),hook("unitTestAfterClass"));append(runnable.back(),hook("unitTestAfterAll"));}catch(...){if(!pending)pending=std::current_exception();}
            try{append(runnable.back(),lifecycle_phase(false));}catch(...){if(!pending)pending=std::current_exception();}
        }
        objects.erase(oid);buffer_ids.erase(receiver);if(pending)std::rethrow_exception(pending);
        if(!access->attempts.empty())throw std::runtime_error("Native access was blocked");
        json results=json::array();size_t failed=0,skipped=0;
        for(const auto& name:selected_methods){auto& method=methods.at(effective.at(name));auto state=statuses.at(name);failed+=state=="failed";skipped+=state=="skipped";
            json result={{"owner",owner["name"]},{"method",name},{"status",state},{"source",method["source_path"]},{"line",method["line"]},{"column",1},{"message",state=="skipped"?"Ignored test or class setup failed":""}};
            if(!issues.at(name).empty()){auto& first=issues.at(name)[0];for(auto key:{"source","line","column","stack"})if(first.contains(key))result[key]=first[key];std::string message;for(const auto& issue:issues.at(name)){if(!message.empty())message+='\n';message+=issue["phase"].get<std::string>()+": "+(issue.value("message",std::string{}).empty()?issue["operation"].get<std::string>()+" failed: "+issue["values"].dump():issue["message"].get<std::string>());}result["message"]=message;}
            results.push_back(result);
        }
        return {{"success",failed==0},{"testsSelected",selected_methods.size()},{"testsCompleted",true},{"cleanupComplete",true},{"results",results},{"failures",failed},{"skipped",skipped},{"assertions",assertions.size()},
            {"nativeInterpreter",true},{"databaseNeverOpened",true},{"accessAttempts",access->attempts},{"pythonProcessesStarted",0}};
    }
};
json native_operation(Context& context){json result;{NativeProgram program(context);result=program.compile();if(result["success"]&&context.request["operation"]!="compile")result.update(context.request["operation"]=="test.run"?program.run_tests():program.execute());}
    result["nativeValueStorage"]="DskParam";result["nativeValueCaptures"]=NativeValue::capture_count();result["liveNativeValuesAfterJob"]=NativeValue::live_count();
    // Message resources are read after native access guards have been restored.
    if(result.contains("diagnostics")&&!result["diagnostics"].empty()){auto module=GetModuleHandleW(L"jomutil.dll");for(auto& diagnostic:result["diagnostics"]){wchar_t text[2048]{};invoke<int>(exported(module,"jomGetMessageText"),diagnostic["code"].get<int>(),text,uint32_t(2048));diagnostic["message"]=utf8(text);}}return result;}
json native_value_test(Context& context){
    auto directory=context.install/L"bin";SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);AddDllDirectory(directory.c_str());if(!LoadLibraryExW((directory/L"jom.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)||!LoadLibraryExW((directory/L"jomutil.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))throw std::runtime_error("Cannot load native value library");AccessGuard guard(context.install);auto before=NativeValue::live_count(),captures=NativeValue::capture_count();bool copied=true,fixed=true;json checks=json::object();
    {std::array<word,66> input{};auto address=reinterpret_cast<word>(input.data());
     wchar_t text[]={L'a',0,L'b',0};at<uint16_t>(address)=DSKCHARBLOCKPOINTER;at<word>(address,8)=reinterpret_cast<word>(text);at<uint32_t>(address,20)=3;auto string=NativeValue::capture(address);text[0]=L'x';Character* read{};Size length{};auto status=paramGetString(*reinterpret_cast<const ShortDskParam*>(string.address()),read,&length);checks["string"]=!status&&length==3&&read[0]==L'a'&&read[1]==0&&read[2]==L'b';copied&=checks["string"].get<bool>();checks["stringLength"]=length;
     unsigned char bytes[]={0,1,255};std::memset(input.data(),0,sizeof(input));at<uint16_t>(address)=DSKBINARYBLOCKPOINTER;at<word>(address,8)=reinterpret_cast<word>(bytes);at<uint32_t>(address,20)=3;auto binary=NativeValue::capture(address);bytes[1]=99;BYTE* output{};Size size{};status=paramGetBinary(*reinterpret_cast<const ShortDskParam*>(binary.address()),output,size);checks["binary"]=!status&&size==3&&output[1]==1;copied&=checks["binary"].get<bool>();
     std::string unicode="a\xc2\xa9";std::memset(input.data(),0,sizeof(input));at<uint16_t>(address)=DSKSTRINGUTF8;at<word>(address,8)=reinterpret_cast<word>(unicode.data());at<uint32_t>(address,16)=3;at<uint32_t>(address,20)=2;at<uint32_t>(address,24)=3;auto utf=NativeValue::capture(address);unicode[0]='x';StringUtf8Info decoded{};status=paramGetStringUtf8Ref(*reinterpret_cast<const ShortDskParam*>(utf.address()),decoded);checks["utf8"]=!status&&decoded.size==3&&decoded.utf8BytePtr[0]=='a';copied&=checks["utf8"].get<bool>();
     for(uint16_t format:{uint16_t(DSKINTEGER),uint16_t(DSKBOOLEAN),uint16_t(DSKDATE),uint16_t(DSKINTEGER64),uint16_t(DSKREAL),uint16_t(DSKDECIMAL),uint16_t(DSKOBJECTID)}){std::memset(input.data(),0,sizeof(input));at<uint16_t>(address)=format;auto value=NativeValue::capture(address);auto again=NativeValue::capture(value.address());fixed&=value.format()==format&&value.equal(again);}
     for(int i=0;i<200;i++){auto copy=NativeValue::capture(string.address());copied&=copy.equal(string);}
     auto scalar=NativeValue(42);std::memset(input.data(),0,sizeof(input));at<uint16_t>(address)=DSKPARAMLIST;at<uint16_t>(address,8)=2;at<word>(address,16)=string.address();at<word>(address,24)=scalar.address();auto list=NativeValue::capture(address);copied&=list.elements().size()==2&&list.elements()[0].equal(string)&&list.elements()[1].integer()==42;
    }
    auto after=NativeValue::live_count();return {{"success",copied&&fixed&&before==after&&guard.attempts.empty()},{"deepCopies",copied},{"checks",checks},{"genericFixedValues",fixed},{"liveBefore",before},{"liveAfter",after},{"captures",NativeValue::capture_count()-captures},{"databaseNeverOpened",true},{"accessAttempts",guard.attempts}};
}
json native_access_test(Context& context){
    auto directory=context.install/L"bin";SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);AddDllDirectory(directory.c_str());
    for(auto name:{L"jom.dll",L"jlncomp.dll",L"jlnctab.dll",L"jlnintp.dll"})if(!LoadLibraryExW((directory/name).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))throw std::runtime_error("Cannot load guard test library");
    auto sockets=LoadLibraryExW(L"ws2_32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!sockets)throw std::runtime_error("Cannot load socket guard control");
    NativeHooks control;USHORT callback_depth{};auto bridge=control.callback("unwind-control",0,[&](const std::array<word,10>&){void* stack[32]{};callback_depth=native_backtrace(0,32,stack,nullptr);return word(0);});DWORD64 image_base{};auto registered=RtlLookupFunctionEntry(bridge+8,&image_base,nullptr)!=nullptr;invoke<word>(bridge,word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0));
    AccessGuard guard(context.install);auto jom=GetModuleHandleW(L"jom.dll");
    auto initialize=invoke<int>(exported(jom,"jomInitialize"),word(0),word(0),word(0),word(0));
    auto sign_on=invoke<int>(exported(jom,"jomSignOn"),word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0),word(0));
    bool denied=false;for(auto name:{L"jom.dll",L"jomos.dll",L"jomdb.dll",L"jlncomp.dll"}){auto module=GetModuleHandleW(name);if(!module)continue;for(auto& item:imports(module))if(item.name=="CreateFileW"){auto path=context.storage/L"__nonexistent_database__"/L"_control.dat";auto result=invoke<word>(at<word>(item.slot),path.c_str(),DWORD(GENERIC_READ),DWORD(FILE_SHARE_READ),word(0),DWORD(OPEN_EXISTING),DWORD(FILE_ATTRIBUTE_NORMAL),word(0));denied=result==word(-1);break;}if(denied)break;}
    bool dynamic_file=false,dynamic_socket=false;
    for(auto& item:imports(GetModuleHandleW(L"jomos.dll")))if(item.name=="GetProcAddress"){
        auto resolver=at<word>(item.slot);auto kernel=GetModuleHandleW(L"kernel32.dll");auto file=invoke<word>(resolver,kernel,"CreateFileW");auto socket=invoke<word>(resolver,sockets,"socket");
        auto target=context.storage/L"__nonexistent_database__"/L"_control.dat";
        dynamic_file=invoke<word>(file,target.c_str(),DWORD(GENERIC_READ),DWORD(FILE_SHARE_READ),word(0),DWORD(OPEN_EXISTING),DWORD(FILE_ATTRIBUTE_NORMAL),word(0))==word(-1);
        dynamic_socket=invoke<word>(socket,word(2),word(1),word(6))==word(-1);break;
    }
    return {{"success",initialize==1219&&sign_on==1219&&denied&&dynamic_file&&dynamic_socket},{"callbackUnwindRegistered",registered},{"callbackStackDepth",callback_depth},{"initializeStatus",initialize},{"signOnStatus",sign_on},{"fileOpenDenied",denied},{"dynamicFileOpenDenied",dynamic_file},{"dynamicSocketDenied",dynamic_socket},{"accessAttempts",guard.attempts},{"guardedEntries",guard.entries}};
}
}


