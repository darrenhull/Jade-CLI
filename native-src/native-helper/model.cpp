#include "helper.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#pragma comment(lib,"bcrypt.lib")
namespace jade {
std::string utf8(const std::wstring& value) {
    if(value.empty()) return {};
    int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!count) throw std::runtime_error("Invalid Unicode text");
    std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),count,nullptr,nullptr);return result;
}
std::wstring wide(const std::string& value) {
    if(value.empty()) return {};
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(!count) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),result.data(),count);return result;
}
fs::path filepath(const std::string& value) {return fs::path(wide(value));}
std::string filename(const fs::path& value) {return utf8(value.wstring());}
std::string read(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);if(!input) throw std::runtime_error("Cannot read: "+filename(path));
    std::string result((std::istreambuf_iterator<char>(input)),{});
    if(result.size()>=3 && result.substr(0,3)=="\xef\xbb\xbf") result.erase(0,3);
    return result;
}
json read_json(const fs::path& path){return json::parse(read(path));}
void write_json(const fs::path& path,const json& value) {
    fs::create_directories(path.parent_path());
    auto temporary=path;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<value.dump();output.flush();if(!output) throw std::runtime_error("Cache write failed");}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary);throw std::runtime_error("Atomic cache replacement failed");
    }
}
std::string hash(const std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE digest{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider failed");
    DWORD object_size{},got{};BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),4,&got,0);
    std::vector<unsigned char> object(object_size);unsigned char output[32]{};
    if(BCryptCreateHash(algorithm,&digest,object.data(),object_size,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA256 creation failed");}
    auto code=BCryptHashData(digest,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),0);
    if(code>=0)code=BCryptFinishHash(digest,output,32,0);
    BCryptDestroyHash(digest);BCryptCloseAlgorithmProvider(algorithm,0);
    if(code<0)throw std::runtime_error("SHA256 failed");
    static const char* digits="0123456789abcdef";std::string result;
    for(auto byte:output){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
std::string file_hash(const fs::path& path){return hash(read(path));}
std::string canonical(const fs::path& path) {
    auto result=filename(fs::weakly_canonical(fs::absolute(path)));
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return char(std::tolower(c));});return result;
}
std::string lexical_key(const fs::path& path){auto result=filename(fs::absolute(path).lexically_normal());std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return char(std::tolower(c));});return result;}
bool within(const fs::path& root,const fs::path& child) {
    auto parent=jade::canonical(root),value=jade::canonical(child);return value==parent || value.starts_with(parent+"\\");
}
Context::Context(const json& input):request(input) {
    install=filepath(input.at("installPath").get<std::string>());storage=filepath(input.at("storagePath").get<std::string>());
    for(const auto& root:input.at("workspaceRoots"))roots.push_back(fs::weakly_canonical(fs::absolute(filepath(root.get<std::string>()))));
    if(roots.empty())throw std::runtime_error("At least one workspace root is required");
    for(const auto& root:roots)if(within(root,storage))throw std::runtime_error("System cache storage must be outside the workspace");
    json libraries=json::object();for(auto name:{"jom.dll","jlncomp.dll","jlnintp.dll","jlnctab.dll"})libraries[name]=file_hash(install/L"bin"/name);
    fingerprint=hash(libraries.dump());
}
fs::path Context::cache_path()const{return storage/L"system-types"/fingerprint/L"definitions.json";}
fs::path Context::index_path()const{json paths=json::array();for(const auto& root:roots)paths.push_back(jade::canonical(root));return storage/L"workspaces"/hash(paths.dump())/L"index.json";}
json import_cache(Context& context) {
    auto input=read_json(filepath(context.request.at("snapshot").get<std::string>()));
    for(auto name:{"jom.dll","jlncomp.dll"})if(input.at("native_libraries").at(name)!=file_hash(context.install/L"bin"/name))throw std::runtime_error("Snapshot/native DLL mismatch");
    auto root=input.at("root_schema_oid").get<std::string>();
    json types=json::array();
    for(auto& [oid,record]:input.at("types").items()) {
        if(record.value("schema_oid",std::string{})!=root)continue;
        auto system=record;
        for(auto& method:system["methods"]){method.erase("execution_source");method.erase("execution_source_error");}
        system["schema_name"]="RootSchema";types.push_back(std::move(system));
    }
    if(types.empty())throw std::runtime_error("Snapshot contains no RootSchema definitions");
    json result={{"formatVersion",1},{"fingerprint",context.fingerprint},{"schema",input.at("schemas").at(root)},{"types",std::move(types)}};
    result["checksum"]=hash(result.dump());write_json(context.cache_path(),result);
    return {{"success",true},{"cachePath",filename(context.cache_path())},{"fingerprint",context.fingerprint},{"systemSchema","RootSchema"},{"typeCount",result["types"].size()}};
}
json system_cache(Context& context) {
    auto cache=read_json(context.cache_path());
    if(cache.value("formatVersion",0)!=1 || cache.value("fingerprint",std::string{})!=context.fingerprint)throw std::runtime_error("System cache fingerprint/version mismatch; import compatible metadata");
    auto checksum=cache.at("checksum");cache.erase("checksum");
    if(checksum!=hash(cache.dump()))throw std::runtime_error("System cache checksum mismatch");
    auto capabilities=cache.value("metadataCapabilities",json::array());if(std::find(capabilities.begin(),capabilities.end(),"constants")!=capabilities.end()&&std::find(capabilities.begin(),capabilities.end(),"unicodeValues")==capabilities.end())throw std::runtime_error("RootSchema cache predates the Unicode value fix; run Jade: Refresh Type Cache");
    cache["checksum"]=checksum;return cache;
}
static int source_line(const std::string& text,size_t offset){return 1+int(std::count(text.begin(),text.begin()+offset,'\n'));}
static json parse_file(const fs::path& file,const std::string& text) {
    static const std::regex schema(R"((?:^|\n)\s*(\w+)\s+subschemaOf\s+(\w+))"),header(R"((?:^|\n)\s*(\w+)\s+subclassOf\s+(\w+)\s*([^;]*);)"),
        definition(R"((?:^|\n)\s*(\w+)\s+(completeDefinition|partialDefinition)\b)"),method(R"((?:^|\n)(\w+)\s*\r?\n\{\r?\n([\s\S]*?)(?:\r?\n)\})"),
        property(R"((?:^|\n)\s*(\w+)\s*:\s*(\w+)(?:\[(\d+(?:\s*,\s*\d+)?)\])?\s+([^;]*?)number\s*=\s*(\d+)\s*,\s*ordinal\s*=\s*(\d+)\s*;)"),
        declaration(R"((?:^|\n)\s*(\w+)\s*\(([^)]*)\)\s*(?::\s*(\w+))?\s*([^;]*?)number\s*=\s*(\d+)\s*;)"),parameter(R"(\s*(\w+)\s*:\s*(\w+)(?:\s+(input|output|io))?\s*)");
    std::smatch s,h,d;json result={{"path",filename(file)},{"methods",json::array()},{"properties",json::array()},{"constants",json::array()}};
    if(std::regex_search(text,s,schema)){result["schema"]=s[1].str();result["parentSchema"]=s[2].str();}
    if(file.extension()==L".sec")return result; // Native schemaDefinitions.sec supplies identity/ancestry, not classes.
    auto split=text.find("typeSources");auto head=text.substr(0,split);
    if(std::regex_search(head,d,definition))result["partial"]=d[2].str()=="partialDefinition";
    if(std::regex_search(head,h,header)) {result["name"]=h[1].str();result["parent"]=h[2].str();result["options"]=h[3].str();result["hasHeader"]=true;}
    else if(std::regex_search(head,d,definition)) {result["name"]=d[1].str();result["partial"]=d[2].str()=="partialDefinition";}
    else return result;
    auto field_section=head.substr(0,head.find("jadeMethodDefinitions"));
    auto constant_start=head.find("constantDefinitions");
    if(constant_start!=std::string::npos){auto section=head.substr(constant_start+19);auto stop=section.find("jadeMethodDefinitions");section=section.substr(0,stop);static const std::regex constant(R"CLS((?:^|\n)\s*(\w+)\s*:\s*(\w+)\s*=\s*((?:"(?:[^"]|"")*"|'(?:[^']|'')*'|[^;"'])+);)CLS");
        for(auto at=std::sregex_iterator(section.begin(),section.end(),constant);at!=std::sregex_iterator();++at){auto match=*at;auto type=match[2].str(),literal=match[3].str();auto begin=literal.find_first_not_of(" \t\r\n"),end=literal.find_last_not_of(" \t\r\n");literal=literal.substr(begin,end-begin+1);json record={{"name",match[1].str()},{"type",type},{"source",literal},{"source_path",filename(file)},{"line",source_line(text,constant_start+19+size_t(match.position(3))+begin)},{"number",result["constants"].size()+1}};
            if(type=="Integer"){try{size_t parsed{};auto value=std::stoi(literal,&parsed);if(parsed==literal.size())record["value"]=value;}catch(const std::exception&){} }
            else if(type=="Boolean"&&(literal=="true"||literal=="false"))record["value"]=literal=="true";
            else if(type=="String"&&literal.size()>=2&&(literal.front()=='\"'||literal.front()=='\'')&&literal.back()==literal.front()&&literal.substr(1,literal.size()-2).find(literal.front())==std::string::npos)record["value"]=literal.substr(1,literal.size()-2);
            result["constants"].push_back(record);}}
    for(auto at=std::sregex_iterator(field_section.begin(),field_section.end(),property);at!=std::sregex_iterator();++at){auto m=*at;auto flags=m[4].str();auto access=flags.find("protected")!=std::string::npos?2:flags.find("readOnly")!=std::string::npos?1:0;result["properties"].push_back({{"name",m[1].str()},{"type",m[2].str()},{"flags",flags},{"access",access},{"subaccess",0},{"length",m[2].str()!="Decimal"&&m[3].matched?std::stoi(m[3].str()):0},{"precision",m[2].str()=="Decimal"&&m[3].matched?std::stoi(m[3].str()):0},{"scaleFactor",m[3].str().find(',')!=std::string::npos?std::stoi(m[3].str().substr(m[3].str().find(',')+1)):0},{"number",std::stoi(m[5].str())},{"ordinal",std::stoi(m[6].str())}});}
    std::map<std::string,json> declarations;
    for(auto at=std::sregex_iterator(head.begin(),head.end(),declaration);at!=std::sregex_iterator();++at) {
        auto m=*at;json record={{"name",m[1].str()},{"parameters",json::array()},{"returnType",m[3].str()},{"flags",m[4].str()},{"number",std::stoi(m[5].str())}};
        std::stringstream params(m[2].str());std::string part;
        while(std::getline(params,part,';')){std::smatch p;if(part.find_first_not_of(" \t\r\n")==std::string::npos)continue;
            if(!std::regex_match(part,p,parameter))throw std::runtime_error("Unsupported parameter in "+filename(file));
            record["parameters"].push_back({{"name",p[1].str()},{"type",p[2].str()},{"usage",p[3].matched?p[3].str():"constant"}});}
        declarations[m[1].str()]=record;
    }
    if(split!=std::string::npos){auto body=text.substr(split);
        for(auto at=std::sregex_iterator(body.begin(),body.end(),method);at!=std::sregex_iterator();++at){auto m=*at;auto name=m[1].str();auto found=declarations.find(name);
            if(found==declarations.end())throw std::runtime_error("Method body lacks declaration: "+name+" in "+filename(file));
            auto record=found->second;record["source"]=m[2].str();record["source_path"]=filename(file);record["line"]=source_line(text,split+size_t(m.position(2)));result["methods"].push_back(record);}
    }
    for(const auto& [name,signature]:declarations){bool present=false;for(const auto& member:result["methods"])if(member["name"]==name)present=true;
        if(!present){auto member=signature;member["source"]="";member["line"]=1;result["methods"].push_back(member);}}
    // An empty inherited/system shell cannot override the original definition.
    if(!result.value("hasHeader",false)&&result["methods"].empty()&&result["properties"].empty()&&result["constants"].empty())result.erase("name");
    return result;
}
static json merge_entries(const json& entries){
    std::map<std::string,std::vector<json>> groups;for(const auto& entry:entries)groups[entry.value("schema",std::string{})+"::"+entry["name"].get<std::string>()].push_back(entry);
    json result=json::array();for(auto& [key,rows]:groups){size_t selected=0;for(size_t i=0;i<rows.size();++i)if(!rows[i].value("partial",false)){selected=i;break;}auto merged=rows[selected];
        merged["fragments"]=json::array();std::map<std::string,json> methods,fields,constants;std::map<int,std::string> numbers,ordinals,method_numbers;
        for(const auto& row:rows){merged["fragments"].push_back(row["path"]);
            for(auto constant:row.value("constants",json::array())){auto name=constant["name"].get<std::string>();constant.erase("number");if(constants.contains(name)&&constants.at(name)!=constant)throw std::runtime_error("Conflicting partial constant "+key+"::"+name);constants[name]=constant;}

            if(row.value("hasHeader",false)&&merged.value("hasHeader",false)&&row["parent"]!=merged["parent"])throw std::runtime_error("Conflicting parent class: "+key);
            for(const auto& field:row["properties"]){auto name=field["name"].get<std::string>();int number=field["number"],ordinal=field["ordinal"];
                if(fields.contains(name)){if(fields.at(name)!=field)throw std::runtime_error("Conflicting partial property "+key+"::"+name);continue;}
                if(numbers.contains(number)||ordinals.contains(ordinal))throw std::runtime_error("Conflicting property number/ordinal in "+key);
                fields[name]=field;numbers[number]=name;ordinals[ordinal]=name;}
            for(auto method:row["methods"]){auto name=method["name"].get<std::string>();int number=method["number"];method["source_path"]=method.value("source_path",row["path"].get<std::string>());
                if(methods.contains(name)){auto& previous=methods.at(name);if(previous["parameters"]!=method["parameters"]||previous["returnType"]!=method["returnType"]||previous["number"]!=method["number"]||previous["flags"]!=method["flags"])throw std::runtime_error("Conflicting partial method signature "+key+"::"+name);
                    if(!previous.value("source",std::string{}).empty()&&!method.value("source",std::string{}).empty())throw std::runtime_error("Duplicate partial method body "+key+"::"+name);if(previous.value("source",std::string{}).empty())previous=method;continue;}
                if(method_numbers.contains(number))throw std::runtime_error("Conflicting partial method number in "+key);methods[name]=method;method_numbers[number]=name;}
        }
        merged["constants"]=json::array();for(auto [name,constant]:constants){constant["number"]=merged["constants"].size()+1;merged["constants"].push_back(constant);}
        merged["properties"]=json::array();for(const auto& [name,field]:fields)merged["properties"].push_back(field);
        merged["methods"]=json::array();for(const auto& [name,method]:methods)merged["methods"].push_back(method);
        // Declaration order matters for test selection/lifecycle attribution.
        std::sort(merged["methods"].begin(),merged["methods"].end(),[](const json& a,const json& b){return a["number"].get<int>()<b["number"].get<int>();});result.push_back(merged);
    }return result;
}
json refresh_index(Context& context) {
    auto start=std::chrono::steady_clock::now();json old=json::object(),previous_dependencies=json::object();
    try{auto saved=read_json(context.index_path());if(saved.value("formatVersion",0)==10 && saved.value("fingerprint",std::string{})==context.fingerprint){old=saved.at("files");previous_dependencies=saved.value("dependencies",json::object());}}catch(const std::exception&){}
    json files=json::object(),entries=json::array(),parents=json::object();size_t parsed=0,reused=0,read_files=0;
    std::set<std::string> force;for(const auto& file:context.request.value("validateFiles",json::array()))force.insert(jade::canonical(filepath(file)));
    if(context.request.contains("source"))force.insert(jade::canonical(filepath(context.request["source"])));
    std::set<std::string> seen,changed;
    auto changed_type=[&](const json& definition){if(definition.contains("name"))changed.insert(definition.value("schema",std::string{})+"::"+definition["name"].get<std::string>());};
    for(const auto& root:context.roots)for(fs::recursive_directory_iterator at(root,fs::directory_options::skip_permission_denied),end;at!=end;++at){
        if(at->is_symlink()){if(at->is_directory())at.disable_recursion_pending();continue;}
        if(at->is_directory()){auto name=filename(at->path().filename());if(name==".git"||name=="node_modules"||name==".jade-cache"||fs::exists(at->path()/L".jade-extraction"))at.disable_recursion_pending();continue;}
        auto extension=filename(at->path().extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension!=".cls"&&!(extension==".sec"&&filename(at->path().filename())=="schemaDefinitions.sec"))continue;
        auto absolute=fs::absolute(at->path());auto key=lexical_key(absolute);if(!seen.insert(key).second)continue;
        auto size=at->file_size();auto modified=at->last_write_time().time_since_epoch().count();std::string digest;json parsed_file;
        if(!context.request.value("verifyContent",false)&&!force.contains(key)&&old.contains(key)&&old[key].value("bytes",uintmax_t(-1))==size&&old[key].value("modified",int64_t(0))==modified){parsed_file=old[key]["definition"];digest=old[key]["hash"];++reused;}
        else{auto text=read(absolute);digest=hash(text);++read_files;if(old.contains(key)&&old[key].value("hash",std::string{})==digest){parsed_file=old[key]["definition"];++reused;}
            else{parsed_file=parse_file(absolute,text);++parsed;changed_type(parsed_file);if(old.contains(key))changed_type(old[key]["definition"]);}}
        files[key]={{"hash",digest},{"definition",parsed_file},{"bytes",size},{"modified",modified}};
        if(parsed_file.contains("schema")){auto schema=parsed_file["schema"].get<std::string>();auto parent=parsed_file["parentSchema"];
            if(parents.contains(schema)&&parents[schema]!=parent)throw std::runtime_error("Conflicting parent schema: "+schema);parents[schema]=parent;}
        if(parsed_file.contains("name"))entries.push_back(parsed_file);
    }
    std::map<std::string,std::string> complete;
    for(const auto& entry:entries){auto key=entry.value("schema",std::string{})+"::"+entry["name"].get<std::string>();
        if(!entry.value("partial",false)){if(complete.contains(key))throw std::runtime_error("Duplicate complete declaration "+key+": "+complete[key]+" and "+entry["path"].get<std::string>());complete[key]=entry["path"];}}
    size_t removed=0;for(auto& [key,value]:old.items())if(!files.contains(key)){++removed;changed_type(value["definition"]);}
    entries=merge_entries(entries);std::set<std::string> identities;for(const auto& entry:entries)identities.insert(entry["schema"].get<std::string>()+"::"+entry["name"].get<std::string>());
    auto resolve=[&](std::string schema,const std::string& name){std::set<std::string> seen;while(!schema.empty()&&seen.insert(schema).second){auto key=schema+"::"+name;if(identities.contains(key))return key;schema=parents.value(schema,std::string{});}return std::string{};};
    json dependencies=json::object();static const std::regex word(R"(\b[A-Za-z_]\w*\b)");
    for(const auto& entry:entries){auto schema=entry["schema"].get<std::string>(),key=schema+"::"+entry["name"].get<std::string>();std::set<std::string> names;if(entry.contains("parent"))names.insert(entry["parent"]);
        for(const auto& field:entry["properties"])names.insert(field["type"]);for(const auto& method:entry["methods"]){auto source=method.value("source",std::string{});for(auto at=std::sregex_iterator(source.begin(),source.end(),word);at!=std::sregex_iterator();++at)names.insert(at->str());auto result=method.value("returnType",std::string{});if(!result.empty())names.insert(result);for(const auto& parameter:method["parameters"])names.insert(parameter["type"]);}
        for(const auto& constant:entry.value("constants",json::array())){names.insert(constant["type"]);auto source=constant.value("source",std::string{});for(auto at=std::sregex_iterator(source.begin(),source.end(),word);at!=std::sregex_iterator();++at)names.insert(at->str());}
        dependencies[key]=json::array();for(const auto& name:names){auto dependency=resolve(schema,name);if(!dependency.empty()&&dependency!=key)dependencies[key].push_back(dependency);}}
    std::map<std::string,std::set<std::string>> reverse;for(const auto& graph:{previous_dependencies,dependencies})for(auto& [owner,targets]:graph.items())for(const auto& target:targets)reverse[target.get<std::string>()].insert(owner);
    std::vector<std::string> queue(changed.begin(),changed.end());for(size_t i=0;i<queue.size();++i)for(const auto& dependent:reverse[queue[i]])if(changed.insert(dependent).second)queue.push_back(dependent);
    write_json(context.index_path(),{{"formatVersion",10},{"fingerprint",context.fingerprint},{"files",files},{"parents",parents},{"dependencies",dependencies}});
    return {{"success",true},{"indexPath",filename(context.index_path())},{"parsedFiles",parsed},{"reusedFiles",reused},{"removedFiles",removed},{"entries",entries},{"parents",parents},
        {"affectedTypes",changed},{"readFiles",read_files},
        {"elapsedMs",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()}};
}
json resolve_type(Context& context,const std::string& schema,const std::string& name) {
    auto index=refresh_index(context);auto current=schema;std::set<std::string> seen;
    while(!current.empty()&&seen.insert(current).second){
        std::vector<json> matches;for(const auto& entry:index["entries"])if(entry.value("schema",std::string{})==current&&entry["name"]==name)matches.push_back(entry);
        if(!matches.empty()) {if(matches.size()!=1)throw std::runtime_error("Partial class assembly is pending: "+current+"::"+name);return {{"success",true},{"definition",matches[0]}};}
        current=index["parents"].value(current,std::string{});
    }
    auto cache=system_cache(context);for(const auto& type:cache["types"])if(type["name"]==name)return {{"success",true},{"definition",type},{"origin","system"}};
    throw std::runtime_error("Type source missing: "+schema+"::"+name);
}
}
