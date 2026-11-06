// Experimental debugger transport. Execution and stepping remain in Jade.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#define DSKPARAM_LINKAGE
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <jomcalls.h>
#include <jomparam.hpp>
#include <jomstr.h>
#include <json.hpp>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
using json = nlohmann::json;
using word = uintptr_t;
using Oid = DskObjectId;
static_assert(sizeof(DskParam) == 528 && sizeof(Oid) == 22 && sizeof(DskHandle) == 16);
std::wstring wide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}
template <class T> T &at(void *p, size_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<char *>(p) + offset);
}
template <class T> T proc(HMODULE m, const char *n)
{
    auto p = GetProcAddress(m, n);
    if (!p)
        throw std::runtime_error(std::string("Native export missing: ") + n);
    return reinterpret_cast<T>(p);
}
template <class R, class... A> R call(word p, A... args)
{
    return reinterpret_cast<R (*)(A...)>(p)(args...);
}
std::mutex outputMutex;
void emit(json v)
{
    v["protocol"] = "jade-debug/1";
    std::lock_guard lock(outputMutex);
    std::cout << v.dump() << std::endl;
}
std::string hex(const void *p, size_t n)
{
    static const char *d = "0123456789abcdef";
    std::string r;
    auto b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++)
    {
        r += d[b[i] >> 4];
        r += d[b[i] & 15];
    }
    return r;
}
std::string digest(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot read native runtime file");
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), {});
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE h{};
    unsigned char out[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable");
    auto code = BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    if (code >= 0)
        code = BCryptHashData(h, data.data(), (ULONG)data.size(), 0);
    if (code >= 0)
        code = BCryptFinishHash(h, out, 32, 0);
    if (h)
        BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (code < 0)
        throw std::runtime_error("SHA256 failed");
    return hex(out, 32);
}
struct P
{
    DskParam v{};
    std::shared_ptr<std::wstring> text;
    P(int format = 0)
    {
        memset(&v, 0, sizeof(v));
        v.header.format = (PFormat)format;
    }
    static P str(const std::string &s)
    {
        P p(1);
        p.text = std::make_shared<std::wstring>(wide(s));
        at<const wchar_t *>(&p.v, 8) = p.text->c_str();
        return p;
    }
    static P integer(int n)
    {
        P p(4);
        at<int>(&p.v, 8) = n;
        return p;
    }
    static P boolean(bool b)
    {
        P p(7);
        at<bool>(&p.v, 8) = b;
        return p;
    }
    static P oid(const Oid &o)
    {
        P p(3);
        memcpy((char *)&p.v + 8, &o, 22);
        return p;
    }
    Oid object() const
    {
        Oid o{};
        if (paramGetOid(v, o))
            throw std::runtime_error("Native value is not an object reference.");
        return o;
    }
    int integer() const
    {
        return *(const int *)((const char *)&v + 8);
    }
};
struct Host
{
    HMODULE jom{}, util{}, interp{}, os{};
    json abi;
    DskHandle node{};
    bool opened = false, aborted = false;
    Host(const json &c)
    {
        if (c.value("workingWrites", false))
        {
            if (!c.contains("root") || !c.contains("sourceDatabasePath"))
                throw std::runtime_error(
                    "Writable debugging requires a private working database and source path.");
            auto database = std::filesystem::canonical(std::filesystem::path(wide(c.at("databasePath"))));
            auto root = std::filesystem::canonical(std::filesystem::path(wide(c.at("root"))));
            auto source = std::filesystem::canonical(std::filesystem::path(wide(c.at("sourceDatabasePath"))));
            auto relative = database.lexically_relative(root);
            if (std::filesystem::equivalent(database, source) || relative.empty() || relative.is_absolute() ||
                *relative.begin() == L"..")
                throw std::runtime_error("Writable debugging is restricted to the private working database, "
                                         "never the source database.");
        }
        auto bin = std::filesystem::path(wide(c.at("installPath"))) / L"bin";
        std::ifstream f(wide(c.at("abi")));
        f >> abi;
        for (auto i = abi["libraries"].begin(); i != abi["libraries"].end(); ++i)
            if (digest(bin / wide(i.key())) != i.value().get<std::string>())
                throw std::runtime_error("Experimental debugger does not support this Jade DLL build: " +
                                         i.key());
        AddDllDirectory(bin.c_str());
        auto load = [&](const wchar_t *n) {
            auto h = LoadLibraryExW((bin / n).c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!h)
                throw std::runtime_error("Cannot load Jade runtime");
            return h;
        };
        jom = load(L"jom.dll");
        util = load(L"jomutil.dll");
        interp = load(L"jlnintp.dll");
        os = load(L"jomos.dll");
        P db = P::str(c.at("databasePath")), server = P::str("singleUser"), ini = P::str(c.at("iniPath"));
        check(proc<decltype(&jomInitialize)>(jom, "jomInitialize")(&node, &db.v, &server.v, &ini.v),
              "initialize");
        opened = true;
    }
    word symbol(const std::string &name)
    {
        return (word)interp + abi["symbols"].at(name).get<word>();
    }
    size_t layout(const char *name) const
    {
        return abi.at("layouts").at(name).get<size_t>();
    }
    void check(int code, const char *op)
    {
        if (!code)
            return;
        wchar_t text[2048]{};
        proc<int (*)(int, wchar_t *, unsigned)>(util, "jomGetMessageText")(code, text, 2048);
        throw std::runtime_error(std::string(op) + ": Jade " + std::to_string(code) + " " + utf8(text));
    }
    ~Host()
    {
        // Jade 25.0.02 can hang in jomFinalize after an interpreter abort, even
        // after both jomSignOff calls complete. This process hosts one debug
        // session only: sessions and owned threads have already been closed.
        if (aborted)
            ExitProcess(0);
        if (opened)
            proc<decltype(&jomFinalize)>(jom, "jomFinalize")(&node);
    }
};
struct Session
{
    Host &h;
    DskHandle process{}, security{};
    std::array<Oid, 8> instances{};
    bool signedOn = false;
    Session(Host &host, const std::string &schema, bool writable = false,
            const std::string &application = "RootSchemaApp")
        : h(host)
    {
        P sc = P::str(schema), app = P::str(application), user = P::str(""), pass = P::str(""),
          mode = P::integer(0), usage = P::integer(writable ? 0 : 1);
        h.check(proc<decltype(&jomSignOn)>(h.jom, "jomSignOn")(&h.node, &process, &security, &sc.v, &app.v,
                                                               &user.v, &pass.v, &mode.v, &usage.v, 0),
                "sign on");
        signedOn = true;
        h.check(proc<decltype(&jomGetExecInstances)>(h.jom, "jomGetExecInstances")(
                    &process, &instances[0], &instances[1], &instances[2], &instances[3], &instances[4],
                    &instances[5], &instances[6], &instances[7], 0),
                "execution instances");
    }
    void send(const Oid &o, const std::string &name, std::vector<P *> args, P &result)
    {
        P selector = P::str(name), list(30);
        at<uint16_t>(&list.v, 8) = (uint16_t)args.size();
        at<uint32_t>(&list.v, 12) = (uint32_t)args.size() * 8;
        for (size_t i = 0; i < args.size(); i++)
            at<DskParam *>(&list.v, 16 + i * 8) = &args[i]->v;
        DskParam *input = args.empty() ? nullptr : args.size() == 1 ? &args[0]->v : &list.v;
        h.check(
            proc<decltype(&jomSendMsg)>(h.jom, "jomSendMsg")(&process, &o, &selector.v, input, &result.v, 0),
            name.c_str());
    }
    P get(const Oid &o, const std::string &n, int format = 0)
    {
        P p(format);
        send(o, n, {}, p);
        return p;
    }
    void invoke(const Oid &o, const std::string &n, std::vector<P *> args = {})
    {
        P p;
        send(o, n, args, p);
    }
    Oid create(unsigned number)
    {
        Oid o{};
        at<unsigned>(&o, 8) = number;
        h.check(proc<decltype(&jomCreateObject)>(h.jom, "jomCreateObject")(&process, &o, CLASS_TRANSIENT,
                                                                           nullptr, nullptr, 0),
                "create transient receiver");
        return o;
    }
    ~Session()
    {
        if (signedOn)
            proc<decltype(&jomSignOff)>(h.jom, "jomSignOff")(&process, 0);
    }
};
struct Wrapper
{
    Host &h;
    std::string kind;
    alignas(8) char data[64]{};
    Wrapper(Host &host, const std::string &k, const Oid *oid = nullptr) : h(host), kind(k)
    {
        proc<void *(*)(void *)>(h.jom, ("??0" + k + "@@QEAA@XZ").c_str())(data);
        if (oid)
            memcpy(data + 16, oid, 22);
    }
    ~Wrapper()
    {
        proc<void (*)(void *)>(h.jom, ("??1" + kind + "@@UEAA@XZ").c_str())(data);
    }
    Oid oid()
    {
        Oid o{};
        memcpy(&o, data + 16, 22);
        return o;
    }
    std::string name()
    {
        wchar_t buf[256]{};
        if (kind == "DskSchema")
        {
            h.check(proc<int (*)(void *, wchar_t *)>(h.jom, "?getName@DskSchema@@UEBAHQEA_W@Z")(data, buf),
                    "schema name");
            return utf8(buf);
        }
        h.check(proc<int (*)(void *, wchar_t *, unsigned)>(h.jom, "?getName@DskSchemaEntity@@UEBAHQEA_WI@Z")(
                    data, buf, 0),
                "metadata name");
        return utf8(buf);
    }
};
json methodInfo(Host &h, const Oid &oid)
{
    Wrapper method(h, "DskMethod", &oid), type(h, "DskType"), schema(h, "DskSchema"),
        script(h, "DskScript", &oid);
    h.check(proc<int (*)(void *, void *)>(h.jom, "?getSchemaType@DskFeature@@UEBAHAEAVDskType@@@Z")(
                method.data, type.data),
            "method owner");
    h.check(proc<int (*)(void *, void *)>(h.jom, "?getSchema@DskType@@UEBAHAEAVDskSchema@@@Z")(type.data,
                                                                                               schema.data),
            "owner schema");
    unsigned size{};
    h.check(
        proc<int (*)(void *, unsigned *)>(h.jom, "?getSourceSize@DskScript@@QEBAHPEAI@Z")(script.data, &size),
        "method source size");
    std::wstring source(size + 1, 0);
    h.check(proc<int (*)(void *, wchar_t *)>(h.jom, "?getSource@DskScript@@QEBAHQEA_W@Z")(script.data,
                                                                                          source.data()),
            "method source");
    source.resize(wcslen(source.c_str()));
    return {{"oid", hex(&oid, 22)},
            {"schema", schema.name()},
            {"owner", type.name()},
            {"method", method.name()},
            {"source", utf8(source)}};
}
json displayNative(const DskParam &native)
{
    auto format = native.header.format;
    std::string value, type;
    switch (format)
    {
    case DSKJADESTRING: {
        Size length{};
        if (JADE_STRING_LENGTH(&native.body.dataHandler, length))
            throw std::runtime_error("Cannot read native string length");
        if (length > 1048576)
        {
            value = "<String exceeds debugger preview limit>";
        }
        else
        {
            std::wstring buffer(length + 1, 0);
            if (JADE_GET_STRING(&native.body.dataHandler, buffer.data(), length + 1))
                throw std::runtime_error("Cannot read native string");
            buffer.resize(length);
            value = utf8(buffer);
        }
        type = "String";
        break;
    }
    case DSKINTEGER:
        value = std::to_string(native.body.integer);
        type = "Integer";
        break;
    case DSKBOOLEAN:
        value = native.body.boolean ? "true" : "false";
        type = "Boolean";
        break;
    case DSKOBJECTID:
    case DSKSHORTOBJECTID:
        value = hex((char *)&native + 8, format == DSKOBJECTID ? 22 : 12);
        type = "Object";
        break;
    case DSKCSTRING:
        value = utf8(native.body.pText ? native.body.pText : L"");
        type = "String";
        break;
    case DSKPSTRING:
        value = utf8(std::wstring(native.body.string.dataArea, native.body.string.length));
        type = "String";
        break;
    case DSKCHARBLOCKPOINTER:
        value = utf8(std::wstring(native.body.charBlockPtr.charPtr + native.body.charBlockPtr.offset,
                                  native.body.charBlockPtr.length));
        type = "String";
        break;
    default:
        value = "<native value format " + std::to_string(format) + ">";
        type = "Jade";
        break;
    }
    return {{"value", value}, {"type", type}, {"variablesReference", 0}};
}
json display(P &p)
{
    return displayNative(p.v);
}
// SDK property reads run in the paused worker context; always restore controller TLS.
struct ReadContext
{
    Host &h;
    DskHandle restore;
    ReadContext(Host &host, const DskHandle &target, const DskHandle &previous) : h(host), restore(previous)
    {
        h.check(proc<int (*)(const DskHandle *)>(h.jom, "jomSetThreadContext")(&target),
                "Enter paused object context");
    }
    ~ReadContext()
    {
        proc<int (*)(const DskHandle *)>(h.jom, "jomSetThreadContext")(&restore);
    }
};
struct OwnedAny : P
{
    Host &h;
    OwnedAny(Host &host) : h(host)
    {
        h.check(proc<int (*)(DskParam *, unsigned char)>(h.util, "paramCreateJadeAny")(&v, 3),
                "Create debugger value");
    }
    ~OwnedAny()
    {
        proc<int (*)(DskParam *)>(h.util, "paramDeleteJadeAny")(&v);
    }
};
std::vector<Oid> metadataMembers(Host &h, Wrapper &object, const char *method)
{
    alignas(8) unsigned char storage[56]{};
    proc<void *(*)(void *, unsigned)>(h.jom, "??0OrderedJomColl@@QEAA@I@Z")(storage, 0);
    struct Guard
    {
        Host &h;
        void *data;
        ~Guard()
        {
            proc<void (*)(void *)>(h.jom, "??1OrderedJomColl@@UEAA@XZ")(data);
        }
    } guard{h, storage};
    h.check(proc<int (*)(void *, void *)>(h.jom, method)(object.data, storage),
            "Read debugger metadata members");
    const unsigned count = proc<unsigned (*)(void *)>(h.jom, "?size@OrderedJomColl@@UEBAIXZ")(storage);
    if (count > 100000)
        throw std::runtime_error("Debugger metadata collection is too large.");
    std::vector<Oid> result;
    for (unsigned i = 1; i <= count; i++)
    {
        auto item =
            proc<void *(*)(void *, unsigned)>(h.jom, "?at@OrderedJomColl@@QEBAPEAVJomObj@@I@Z")(storage, i);
        Oid oid{};
        memcpy(&oid, (char *)item + 16, 22);
        result.push_back(oid);
    }
    return result;
}
struct DebugObjects
{
    struct Reference
    {
        Oid oid{}, cls{};
        std::string type;
        bool collection = false, array = false;
        unsigned size = 0, keys = 0;
    };
    struct Property
    {
        std::string name, type;
        bool mapped = false;
    };
    Host &h;
    Session &controller;
    DskHandle &worker;
    std::map<int, Reference> handles;
    std::map<std::string, int> identities;
    std::map<std::string, std::vector<Property>> propertyCache;
    int next = 1;
    void clear()
    {
        handles.clear();
        identities.clear();
    }
    std::vector<Property> properties(const Oid &cls)
    {
        const auto key = hex(&cls, 22);
        if (propertyCache.contains(key))
            return propertyCache.at(key);
        std::map<std::string, Property> members;
        std::set<std::string> visited;
        auto current = cls;
        while (at<const uint64_t>(&current, 0) && visited.insert(hex(&current, 22)).second)
        {
            Wrapper type(h, "DskClass", &current);
            for (const auto &oid : metadataMembers(h, type, "?properties@DskClass@@UEBAHAEAVJomColl@@@Z"))
            {
                Wrapper property(h, "DskProperty", &oid), dataType(h, "DskType");
                auto name = property.name();
                if (name.starts_with('_') || members.contains(name))
                    continue;
                h.check(proc<int (*)(void *, void *)>(h.jom, "?getType@DskFeature@@UEBAHAEAVDskType@@@Z")(
                            property.data, dataType.data),
                        "Read property type");
                bool mapped{};
                h.check(proc<int (*)(void *, bool *)>(h.jom, "?isMapped@DskProperty@@UEBAHPEA_N@Z")(
                            property.data, &mapped),
                        "Read mapped property flag");
                members.emplace(name, Property{name, dataType.name(), mapped});
            }
            Wrapper parent(h, "DskClass");
            h.check(proc<int (*)(void *, void *)>(h.jom, "?getSuperclass@DskClass@@QEBAHAEAV1@@Z")(
                        type.data, parent.data),
                    "Read superclass");
            current = parent.oid();
        }
        std::vector<Property> result;
        for (auto &[name, property] : members)
            result.push_back(property);
        propertyCache[key] = result;
        return result;
    }
    json describe(P &value, const std::string &name, const std::string &declared = "")
    {
        auto result = display(value);
        result["name"] = name;
        if (!declared.empty())
            result["type"] = declared;
        if (value.v.header.format != DSKOBJECTID && value.v.header.format != DSKSHORTOBJECTID)
        {
            if (result["type"] == "Jade" ||
                result["value"].get<std::string>().starts_with("<native value format"))
            {
                // Native Any conversion covers Date, Decimal, Integer64 and other primitives.
                // Object references are handled separately and never sent application String methods.
                OwnedAny text(h);
                auto selector = P::str("asString");
                h.check(
                    proc<decltype(&jomCallPrimMethod)>(h.jom, "jomCallPrimMethod")(
                        &controller.process, DSKANY_PRIMITIVE, &value.v, &selector.v, nullptr, &text.v, 0),
                    "Format native debugger value");
                result["value"] = display(text).at("value");
            }
            return result;
        }
        const auto oid = value.object();
        if (!at<const uint64_t>((void *)&oid, 0))
        {
            result["value"] = "null";
            return result;
        }
        auto identityOid = oid;
        identityOid.edition = 0;
        const auto identity = hex(&identityOid, 22);
        int handle;
        if (identities.contains(identity))
            handle = identities.at(identity);
        else
        {
            if (handles.size() >= 4096)
                throw std::runtime_error("Debugger object expansion limit reached.");
            ReadContext read(h, worker, controller.process);
            Wrapper object(h, "DskObject", &oid), cls(h, "DskClass");
            h.check(proc<int (*)(void *, void *, unsigned)>(
                        h.jom, "?getClass@DskObject@@UEBAHAEAVDskClass@@I@Z")(object.data, cls.data, 0),
                    "Read debug object class");
            Reference reference{oid, cls.oid(), cls.name()};
            auto kind = proc<int (*)(void *, int, bool *)>(h.jom, "?isKindOf@DskObject@@QEBAHHPEA_N@Z");
            h.check(kind(object.data, 70, &reference.collection), "Read collection kind");
            if (reference.collection)
            {
                h.check(kind(object.data, 79, &reference.array), "Read array kind");
                bool dictionary = false;
                h.check(kind(object.data, 74, &dictionary), "Read dictionary kind");
                if (dictionary)
                {
                    Wrapper type(h, "DskCollClass", &reference.cls);
                    reference.keys = static_cast<unsigned>(
                        metadataMembers(h, type, "?keys@DskCollClass@@UEBAHAEAVJomColl@@@Z").size());
                }
                Wrapper collection(h, "DskCollection", &oid);
                h.check(proc<int (*)(void *, unsigned *)>(h.jom, "?getSize@DskCollection@@UEBAHPEAI@Z")(
                            collection.data, &reference.size),
                        "Read collection size");
            }
            handle = next++;
            handles.emplace(handle, reference);
            identities.emplace(identity, handle);
        }
        if (handles.at(handle).collection)
            result["indexedVariables"] = handles.at(handle).size;
        result["namedVariables"] = properties(handles.at(handle).cls).size();
        result["variablesReference"] = handle;
        result["type"] = handles.at(handle).type;
        result["value"] = handles.at(handle).type + " {" + identity + "}";
        return result;
    }
    json expand(int handle, int start, int count, const std::string &filter = "")
    {
        if (!handles.contains(handle))
            throw std::runtime_error("Object reference is stale; pause again to inspect it.");
        if (start < 0 || count < 0 || count > 512)
            throw std::runtime_error("Invalid debugger object page.");
        const auto reference = handles.at(handle);
        const auto fields = filter == "indexed" ? std::vector<Property>{} : properties(reference.cls);
        if (filter != "" && filter != "indexed" && filter != "named")
            throw std::runtime_error("Invalid debugger variable filter.");
        json values = json::array();
        for (size_t i = static_cast<size_t>(start);
             i < fields.size() && i < static_cast<size_t>(start) + static_cast<size_t>(count ? count : 100);
             i++)
        {
            const auto &field = fields[i];
            if (field.mapped)
            {
                values.push_back({{"name", field.name},
                                  {"type", field.type},
                                  {"value", "<mapped property: not evaluated>"},
                                  {"variablesReference", 0}});
                continue;
            }
            try
            {
                OwnedAny value(h);
                {
                    ReadContext read(h, worker, controller.process);
                    Wrapper object(h, "DskObject", &reference.oid);
                    auto propertyName = wide(field.name);
                    h.check(proc<int (*)(void *, const wchar_t *, DskParam *, unsigned)>(
                                h.jom, "?getProperty@DskObject@@QEBAHQEB_WPEAUDskParam@@I@Z")(
                                object.data, propertyName.c_str(), &value.v, 0),
                            "Read native object property");
                }
                values.push_back(describe(value, field.name, field.type));
            }
            catch (const std::exception &e)
            {
                values.push_back({{"name", field.name},
                                  {"type", field.type},
                                  {"value", std::string("<unavailable: ") + e.what() + ">"},
                                  {"variablesReference", 0}});
            }
        }
        if (reference.collection && filter != "named")
        {
            const size_t offset = fields.size();
            const size_t first =
                static_cast<size_t>(start) > offset ? static_cast<size_t>(start) - offset : 0;
            const size_t end = std::min<size_t>(
                reference.size,
                static_cast<size_t>(start) + static_cast<size_t>(count ? count : 100) > offset
                    ? static_cast<size_t>(start) + static_cast<size_t>(count ? count : 100) - offset
                    : 0);
            if (first < end)
            {
                if (reference.array)
                {
                    for (size_t i = first; i < end; i++)
                    {
                        OwnedAny value(h);
                        {
                            ReadContext read(h, worker, controller.process);
                            Wrapper array(h, "DskArray", &reference.oid);
                            h.check(proc<int (*)(void *, unsigned, DskParam *)>(
                                        h.jom, "?at@DskArray@@QEBAHIPEAUDskParam@@@Z")(
                                        array.data, static_cast<unsigned>(i + 1), &value.v),
                                    "Read array entry");
                        }
                        values.push_back(describe(value, "[" + std::to_string(i + 1) + "]"));
                    }
                }
                else
                {
                    struct Entry
                    {
                        std::unique_ptr<OwnedAny> value;
                        std::vector<std::unique_ptr<OwnedAny>> keys;
                    };
                    std::vector<Entry> entries;
                    {
                        ReadContext read(h, worker, controller.process);
                        Wrapper collection(h, "DskCollection", &reference.oid), iterator(h, "DskIterator");
                        h.check(proc<int (*)(void *, void *, unsigned)>(
                                    h.jom, "?createIterator@DskCollection@@QEBAHPEAVDskIterator@@I@Z")(
                                    collection.data, iterator.data, 0),
                                "Create debugger iterator");
                        struct Guard
                        {
                            Host &h;
                            Wrapper &iterator;
                            ~Guard()
                            {
                                proc<int (*)(void *)>(h.jom,
                                                      "?deleteObject@DskObject@@QEBAHXZ")(iterator.data);
                            }
                        } guard{h, iterator};
                        for (size_t i = 0; i < end; i++)
                        {
                            auto value = std::make_unique<OwnedAny>(h);
                            P more(DSKBOOLEAN);
                            h.check(proc<int (*)(void *, const wchar_t *, DskParam *, DskParam *, unsigned)>(
                                        h.jom, "?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z")(
                                        iterator.data, L"next", &value->v, &more.v, 0),
                                    "Read native iterator entry");
                            if (!at<bool>(&more.v, 8))
                                break;
                            if (i >= first)
                            {
                                Entry entry{std::move(value)};
                                for (unsigned k = 1; k <= reference.keys; k++)
                                {
                                    auto key = std::make_unique<OwnedAny>(h);
                                    auto ordinal = P::integer(k);
                                    h.check(proc<int (*)(void *, const wchar_t *, DskParam *, DskParam *,
                                                         unsigned)>(
                                                h.jom, "?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z")(
                                                iterator.data, L"getCurrentKey", &ordinal.v, &key->v, 0),
                                            "Read dictionary key");
                                    entry.keys.push_back(std::move(key));
                                }
                                entries.push_back(std::move(entry));
                            }
                        }
                    }
                    for (size_t i = 0; i < entries.size(); i++)
                    {
                        auto item = describe(*entries[i].value, "[" + std::to_string(first + i + 1) + "]");
                        if (!entries[i].keys.empty())
                        {
                            std::string label;
                            json keys = json::array();
                            for (auto &key : entries[i].keys)
                            {
                                auto formatted = describe(*key, "");
                                if (!label.empty())
                                    label += " · ";
                                label += formatted["value"].get<std::string>();
                                keys.push_back(formatted);
                            }
                            item["key"] = label;
                            item["keys"] = keys;
                            item["name"] = "[" + label + "]";
                        }
                        values.push_back(item);
                    }
                }
            }
        }
        return {{"variables", values},
                {"namedVariables", fields.size()},
                {"indexedVariables", reference.collection ? reference.size : 0}};
    }
};
// Process.currentStack produces Jade's own ordered descriptors and source positions.
// Read them in the paused process, then dispose this inspector-owned transient graph.
struct NativeStack
{
    std::vector<Oid> methods;
    json frames = json::array();
    void clear()
    {
        methods.clear();
        frames = json::array();
    }
    void capture(Host &h, Session &controller, DskHandle &worker, const Oid &processOid, int position)
    {
        struct Item
        {
            Oid method{};
            int position = 0;
        };
        std::vector<Item> items;
        {
            ReadContext read(h, worker, controller.process);
            Oid type{};
            type.classNo = 325;
            Wrapper stack(h, "DskArray", &type), process(h, "DskObject", &processOid);
            h.check(proc<int (*)(void *)>(h.jom, "?createTransientObject@DskObject@@QEAAHXZ")(stack.data),
                    "Create native stack array");
            struct Guard
            {
                Host &h;
                Wrapper &stack;
                ~Guard()
                {
                    proc<int (*)(void *)>(h.jom, "?purge@DskCollection@@QEBAHXZ")(stack.data);
                    proc<int (*)(void *)>(h.jom, "?deleteObject@DskObject@@QEBAHXZ")(stack.data);
                }
            } guard{h, stack};
            auto argument = P::oid(stack.oid());
            P result;
            h.check(proc<int (*)(void *, const wchar_t *, DskParam *, DskParam *, unsigned)>(
                        h.jom, "?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z")(
                        process.data, L"currentStack", &argument.v, &result.v, 0),
                    "Read native process stack");
            unsigned count = 0;
            h.check(proc<int (*)(void *, unsigned *)>(h.jom, "?getSize@DskCollection@@UEBAHPEAI@Z")(
                        stack.data, &count),
                    "Read stack size");
            if (count < 2 || count > 4096)
                throw std::runtime_error("Invalid native stack size.");
            for (unsigned i = 1; i <= count; i++)
            {
                OwnedAny descriptor(h), method(h), sourcePosition(h);
                h.check(proc<int (*)(void *, unsigned, DskParam *)>(
                            h.jom, "?at@DskArray@@QEBAHIPEAUDskParam@@@Z")(stack.data, i, &descriptor.v),
                        "Read stack descriptor");
                const auto oid = descriptor.object();
                Wrapper row(h, "DskObject", &oid);
                auto get = proc<int (*)(void *, const wchar_t *, DskParam *, unsigned)>(
                    h.jom, "?getProperty@DskObject@@QEBAHQEB_WPEAUDskParam@@I@Z");
                h.check(get(row.data, L"method", &method.v, 0), "Read frame method");
                h.check(get(row.data, L"position", &sourcePosition.v, 0), "Read frame position");
                items.push_back({method.object(), sourcePosition.integer()});
            }
        }
        const auto instrumentation = methodInfo(h, items.front().method);
        if (instrumentation["schema"] != "RootSchema" || instrumentation["owner"] != "Process" ||
            instrumentation["method"] != "currentStack")
            throw std::runtime_error("Unexpected native stack instrumentation frame.");
        clear();
        for (size_t i = 1; i < items.size(); i++)
        {
            auto frame = methodInfo(h, items[i].method);
            frame["level"] = methods.size();
            frame["nativeLevel"] = items.size() - i;
            frame["position"] = i == 1 ? position : items[i].position;
            frames.push_back(frame);
            methods.push_back(items[i].method);
        }
    }
};
// Cancellable input lets shutdown join before its queue and mutex are destroyed.
class CommandInbox
{
    static constexpr size_t MaxLine = 1024 * 1024;
    std::mutex mutex;
    std::queue<json> queue;
    std::atomic<bool> closing{false};
    std::thread reader;

    void read()
    {
        std::string pending;
        char buffer[4096];
        DWORD count{}, available{};
        const auto input = GetStdHandle(STD_INPUT_HANDLE);
        while (!closing)
        {
            // Peek avoids a race between entering ReadFile and shutdown.
            if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                break;
            if (!available)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!ReadFile(input, buffer, static_cast<DWORD>(std::min<size_t>(sizeof(buffer), available)),
                          &count, nullptr) ||
                !count)
                break;
            pending.append(buffer, count);
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos)
            {
                const auto line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (line.size() > MaxLine)
                {
                    emit(
                        {{"event", "error"}, {"message", "Native debugger request exceeds its size limit."}});
                    ended = true;
                    return;
                }
                try
                {
                    auto request = json::parse(line);
                    if (!request.is_object() || !request.contains("id") || !request["id"].is_number_integer())
                        continue;
                    std::lock_guard lock(mutex);
                    if (queue.size() >= 256)
                    {
                        emit({{"event", "error"},
                              {"message", "Native debugger command queue limit exceeded."}});
                        ended = true;
                        return;
                    }
                    queue.push(std::move(request));
                }
                catch (const json::exception &)
                {
                    emit({{"event", "error"}, {"message", "Malformed native debugger request."}});
                }
            }
            if (pending.size() > MaxLine)
            {
                emit({{"event", "error"}, {"message", "Native debugger request exceeds its size limit."}});
                break;
            }
        }
        ended = true;
    }

  public:
    std::atomic<bool> ended{false};
    // Start only after every member (including ended) has been initialized.
    CommandInbox()
    {
        reader = std::thread([this] { read(); });
    }
    CommandInbox(const CommandInbox &) = delete;
    ~CommandInbox()
    {
        closing = true;
        if (reader.joinable())
        {
            reader.join();
        }
    }
    json next()
    {
        std::lock_guard lock(mutex);
        if (queue.empty())
            return nullptr;
        auto request = std::move(queue.front());
        queue.pop();
        return request;
    }
};

int wmain(int argc, wchar_t **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected debugger session JSON file");
        json config;
        {
            std::ifstream input{std::filesystem::path(argv[1])};
            input >> config;
        } // Release the private session file before emitting ready.
        Host h(config);
        Session controller(h, "JadeSchema");
        void *context = call<void *>(h.symbol("getContext"));
        auto heap = call<void *>(h.symbol("xheap_INT"));
        auto manager =
            proc<void *(*)(void *, size_t, bool)>(h.os, "?alloc@XHeap@@QEAAPEAX_K_N@Z")(heap, 248, false);
        call<void>(h.symbol("BreakpointManager::BreakpointManager"), manager);
        call<void>(h.symbol("Context::setBreakpointManager"), context, manager);
        auto interpreter = controller.create(DSKJADEINTERPRETER);
        auto debugger = controller.create(DSKJADEDEBUGGER);
        auto prop = P::str("jadeProcess"), pvalue = P::oid(controller.instances[6]);
        h.check(proc<decltype(&jomSetProperty)>(h.jom, "jomSetProperty")(&controller.process, &debugger,
                                                                         &prop.v, &pvalue.v, 0),
                "debugger controller");
        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false, release = false;
        std::atomic<bool> done = false;
        Oid owner{}, selectedMethod{}, workerProcess{};
        DskHandle workerHandle{};
        std::string workerError;
        json returnValue;
        const auto launchMode = config.value("launchMode", std::string{"method"});
        if (launchMode != "method" && launchMode != "application")
            throw std::runtime_error("Unsupported native launch mode.");
        if (launchMode == "application")
        {
            Wrapper root(h, "DskSchema", &controller.instances[2]), schema(h, "DskSchema"),
                definition(h, "DskObject");
            auto schemaName = wide(config.at("schema")), application = wide(config.at("application"));
            h.check(proc<int (*)(void *, const wchar_t *, void *)>(
                        h.jom, "?findSchema@DskSchema@@UEBAHQEB_WAEAV1@@Z")(root.data, schemaName.c_str(),
                                                                            schema.data),
                    "Find application schema");
            h.check(proc<int (*)(void *, const wchar_t *, void *)>(
                        h.jom, "?getApplication@DskSchema@@QEBAHQEB_WAEAVDskObject@@@Z")(
                        schema.data, application.c_str(), definition.data),
                    "Find application definition");
            OwnedAny type(h), initialize(h);
            auto get = proc<int (*)(void *, const wchar_t *, DskParam *, unsigned)>(
                h.jom, "?getProperty@DskObject@@QEBAHQEB_WPEAUDskParam@@I@Z");
            h.check(get(definition.data, L"applicationType", &type.v, 0), "Read application type");
            if (type.v.header.format != DSKCHARACTER ||
                std::wstring(L"SNT").find(type.v.body.character) == std::wstring::npos)
                throw std::runtime_error("GUI applications require a GUI runtime host; native application "
                                         "debugging supports S/N/T non-GUI initialization only.");
            h.check(get(definition.data, L"initializeMethod", &initialize.v, 0),
                    "Read application initializer");
            selectedMethod = initialize.object();
            if (!selectedMethod.instId)
                throw std::runtime_error("Application has no configured initialization method to debug.");
        }
        std::thread worker([&] {
            try
            {
                const bool applicationLaunch =
                    config.value("launchMode", std::string{"method"}) == "application";
                Session s(h, config.at("schema"), config.value("workingWrites", false),
                          applicationLaunch ? config.at("application").get<std::string>() : "RootSchemaApp");
                Oid receiver{}, runner{}, tests{};
                if (!applicationLaunch)
                {
                    auto name = P::str(config.at("owner"));
                    P cls(3);
                    s.send(s.instances[3], "getClass", {&name}, cls);
                    owner = cls.object();
                    if (at<uint64_t>(&owner, 0) == 0)
                        throw std::runtime_error("Class is not present in the working database.");
                    Wrapper type(h, "DskClass", &owner);
                    unsigned number{};
                    h.check(proc<int (*)(void *, unsigned *, unsigned)>(
                                h.jom, "?getNumber@DskSchemaEntity@@UEBAHPEAII@Z")(type.data, &number, 0),
                            "receiver class number");
                    bool abstract = false;
                    h.check(proc<int (*)(void *, bool *)>(
                                h.jom, "?getAbstract@DskSchemaEntity@@UEBAHPEA_N@Z")(type.data, &abstract),
                            "class abstract flag");
                    if (abstract)
                        throw std::runtime_error(
                            "Cannot launch an abstract class directly; choose a concrete subclass.");
                    if (!config.value("unitTest", false))
                        receiver = s.create(number);
                    auto methodName = P::str(config.at("method"));
                    P method(3);
                    s.send(owner, "getMethod", {&methodName}, method);
                    selectedMethod = method.object();
                    if (at<uint64_t>(&selectedMethod, 0) == 0)
                        throw std::runtime_error("Method is not present in the working database.");
                    if (config.value("unitTest", false))
                    {
                        runner = s.create(807);
                        tests = s.create(DSKOBJECTARRAY);
                        auto selected = P::oid(selectedMethod);
                        s.invoke(tests, "add", {&selected});
                    }
                }
                call<void *>(h.symbol("getContext"));
                workerProcess = s.instances[6];
                workerHandle = s.process;
                {
                    std::unique_lock lock(mutex);
                    ready = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return release; });
                }
                P result;
                h.check(proc<int (*)(DskParam *, unsigned char)>(h.util, "paramCreateJadeAny")(&result.v, 3),
                        "return parameter");
                try
                {
                    if (applicationLaunch)
                    {
                        P input;
                        DskParam *parameter = nullptr;
                        if (config.contains("applicationParameter") &&
                            !config["applicationParameter"].is_null())
                        {
                            if (!config["applicationParameter"].is_string())
                                throw std::runtime_error("Application parameter must be a String.");
                            input = P::str(config["applicationParameter"]);
                            parameter = &input.v;
                        }
                        h.check(proc<decltype(&jomInitializeApplication)>(h.jom, "jomInitializeApplication")(
                                    &s.process, true, parameter, 0),
                                "Initialize native application");
                        h.check(proc<decltype(&jomFinalizeApplication)>(h.jom, "jomFinalizeApplication")(
                                    &s.process, true, 0),
                                "Finalize native application");
                        returnValue = {{"type", "Application"},
                                       {"value", "Application completed"},
                                       {"variablesReference", 0}};
                    }
                    else if (config.value("unitTest", false))
                    {
                        auto selection = P::oid(tests);
                        s.send(runner, "runTests", {&selection}, result);
                    }
                    else
                        s.send(receiver, config.at("method"), {}, result);
                    if (!applicationLaunch)
                        returnValue = display(result);
                }
                catch (...)
                {
                    proc<int (*)(DskParam *)>(h.util, "paramDeleteJadeAny")(&result.v);
                    throw;
                }
                proc<int (*)(DskParam *)>(h.util, "paramDeleteJadeAny")(&result.v);
            }
            catch (const std::exception &e)
            {
                workerError = e.what();
                done = true;
                {
                    std::lock_guard lock(mutex);
                    ready = true;
                    cv.notify_all();
                }
            }
            done = true;
        });
        // A fatal transport/setup error must not unwind past a joinable native thread.
        struct JoinGuard
        {
            std::thread &t;
            std::atomic<bool> &done;
            ~JoinGuard()
            {
                if (t.joinable())
                {
                    if (done)
                        t.join();
                    else
                    {
                        emit({{"event", "error"},
                              {"message", "Debugger setup failed while its native worker was active; ending "
                                          "isolated helper."}});
                        ExitProcess(2);
                    }
                }
            }
        } guard{worker, done};
        {
            std::unique_lock lock(mutex);
            if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return ready; }))
                throw std::runtime_error("Debugger worker did not initialise");
        }
        if (done)
            throw std::runtime_error(workerError);
        auto target = P::oid(workerProcess), attaching = P::boolean(true);
        controller.invoke(debugger, "attachProcess", {&target, &attaching});
        auto mode = P::integer(2);
        controller.invoke(debugger, "stepProcess", {&mode});
        {
            std::lock_guard lock(mutex);
            release = true;
            cv.notify_all();
        }
        emit({{"event", "ready"}, {"method", methodInfo(h, selectedMethod)}});
        CommandInbox inbox;
        auto nativeDebugger = at<void *>(context, 0);
        DebugObjects objects{h, controller, workerHandle};
        NativeStack stack;
        int pausedPosition = 0;
        bool paused = false, stopping = false;
        std::string nextStopReason = "step";
        auto stopDeadline = std::chrono::steady_clock::time_point::max();
        auto resumeAt = std::chrono::steady_clock::now();
        Oid active = selectedMethod;
        auto abortNative = [&] {
            auto mode = P::integer(1);
            if (paused)
            {
                controller.invoke(selectedMethod, "resetBreakPoints");
                controller.invoke(debugger, "stepProcess", {&mode});
            }
            else
            {
                P result;
                Wrapper object(h, "DskObject", &debugger);
                h.check(proc<int (*)(void *, const wchar_t *, DskParam *, DskParam *, unsigned)>(
                            h.jom, "?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z")(
                            object.data, L"stepProcess", &mode.v, &result.v, 0),
                        "Stop native debugger");
                h.check(proc<int (*)(const DskHandle *, const Oid *)>(h.jom, "jomInterruptProcess")(
                            &h.node, &workerProcess),
                        "Interrupt native process");
            }
        };
        while (!done)
        {
            json request = inbox.next();
            if (!request.is_null())
            {
                auto id = request.value("id", 0);
                try
                {
                    auto command = request.value("command", "");
                    json body = json::object();
                    if (command == "stack")
                    {
                        if (!paused)
                            throw std::runtime_error("Stack is available only while paused.");
                        if (stack.methods.empty())
                            stack.capture(h, controller, workerHandle, workerProcess, pausedPosition);
                        body["frames"] = stack.frames;
                    }
                    else if (command == "pause")
                    {
                        if (stopping)
                            throw std::runtime_error("Debugger is stopping.");
                        if (!paused)
                        {
                            auto mode = P::integer(0);
                            P result;
                            Wrapper object(h, "DskObject", &debugger);
                            h.check(proc<int (*)(void *, const wchar_t *, DskParam *, DskParam *, unsigned)>(
                                        h.jom, "?sendMsg@DskObject@@QEBAHQEB_WPEAUDskParam@@1I@Z")(
                                        object.data, L"stepProcess", &mode.v, &result.v, 0),
                                    "Pause native debugger");
                            h.check(proc<int (*)(const DskHandle *, const Oid *)>(
                                        h.jom, "jomInterruptProcess")(&h.node, &workerProcess),
                                    "Interrupt native process");
                            nextStopReason = "pause";
                            resumeAt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                        }
                    }
                    else if (command == "resume")
                    {
                        const auto requestedMode = request.value("mode", 2);
                        if (!paused || (requestedMode != 0 && requestedMode != 2 && requestedMode != 3))
                            throw std::runtime_error(
                                "Resume requires a paused process and a supported step mode.");
                        auto step = P::integer(requestedMode);
                        objects.clear();
                        nextStopReason = "step";
                        controller.invoke(debugger, "stepProcess", {&step});
                        paused = false;
                        resumeAt = std::chrono::steady_clock::now();
                    }
                    else if (command == "stepOut")
                    {
                        if (!paused)
                            throw std::runtime_error("Step out requires a paused process.");
                        auto level = P::integer(0), method = P::oid(active);
                        objects.clear();
                        controller.invoke(debugger, "runToReturn", {&level, &method});
                        paused = false;
                        resumeAt = std::chrono::steady_clock::now();
                    }
                    else if (command == "breakpoints")
                    {
                        if (!request.at("positions").is_array() || request.at("positions").size() > 1024)
                            throw std::runtime_error("Invalid breakpoint list.");
                        for (const auto &pos : request.at("positions"))
                            if (!pos.is_number_integer() || pos.get<int>() < 0)
                                throw std::runtime_error(
                                    "A breakpoint position must be a non-negative integer.");
                        controller.invoke(selectedMethod, "resetBreakPoints");
                        json positions = json::array();
                        for (auto pos : request.at("positions"))
                        {
                            auto p = P::integer(pos), enabled = P::boolean(true), condition = P::str(""),
                                 count = P::integer(0);
                            P actual(4);
                            controller.send(selectedMethod, "setBreakpoint",
                                            {&p, &enabled, &condition, &count}, actual);
                            positions.push_back(actual.integer());
                        }
                        body["positions"] = positions;
                    }
                    else if (command == "variables")
                    {
                        if (!paused)
                            throw std::runtime_error(
                                "Variables are available only while native execution is paused.");
                        if (!request.at("names").is_array() || request.at("names").size() > 512)
                            throw std::runtime_error("Invalid variable request.");
                        const int relativeLevel = request.value("level", 0);
                        if (stack.methods.empty())
                            stack.capture(h, controller, workerHandle, workerProcess, pausedPosition);
                        const int stackSize = static_cast<int>(stack.methods.size());
                        if (relativeLevel < 0 || relativeLevel >= stackSize)
                            throw std::runtime_error("Invalid stack frame level.");
                        const auto targetMethod = stack.methods[relativeLevel];
                        json values = json::array();
                        for (auto item : request.at("names"))
                        {
                            if (!item.is_string() || item.get_ref<const std::string &>().size() > 256)
                                throw std::runtime_error("Invalid variable name.");
                            P value;
                            h.check(proc<int (*)(DskParam *, unsigned char)>(h.util, "paramCreateJadeAny")(
                                        &value.v, 3),
                                    "variable parameter");
                            struct AnyGuard
                            {
                                DskParam *value;
                                int (*destroy)(DskParam *);
                                ~AnyGuard()
                                {
                                    destroy(value);
                                }
                            } anyGuard{&value.v, proc<int (*)(DskParam *)>(h.util, "paramDeleteJadeAny")};
                            auto level = P::integer(relativeLevel == 0 ? 0 : stackSize - relativeLevel),
                                 name = P::str(item);
                            P found(7);
                            try
                            {
                                controller.send(targetMethod, "getValueDebuggee", {&level, &name, &value},
                                                found);
                                if (at<bool>(&found.v, 8))
                                {
                                    auto v = objects.describe(value, item);
                                    values.push_back(v);
                                }
                            }
                            catch (const std::exception &e)
                            {
                                values.push_back(
                                    {{"name", item}, {"value", e.what()}, {"variablesReference", 0}});
                            }
                        }
                        body["variables"] = values;
                    }
                    else if (command == "expand")
                    {
                        if (!paused)
                            throw std::runtime_error("Objects are available only while paused.");
                        body = objects.expand(request.at("reference").get<int>(), request.value("start", 0),
                                              request.value("count", 100),
                                              request.value("filter", std::string{}));
                    }
                    else if (command == "stop")
                    {
                        stopping = true;
                        stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                        abortNative();
                    }
                    else
                        throw std::runtime_error("Unknown debugger command");
                    emit({{"id", id}, {"success", true}, {"body", body}});
                }
                catch (const std::exception &e)
                {
                    emit({{"id", id}, {"success", false}, {"message", e.what()}});
                }
            }
            if (inbox.ended && !stopping)
            {
                stopping = true;
                stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                abortNative();
            }
            if (stopping && std::chrono::steady_clock::now() > stopDeadline)
            {
                emit({{"event", "error"},
                      {"message", "Native execution did not stop; terminating its isolated helper."}});
                ExitProcess(2);
            }
            if (!paused && !stopping &&
                std::chrono::steady_clock::now() - resumeAt > std::chrono::milliseconds(70) &&
                at<bool>(nativeDebugger, h.layout("Debugger.waiting")))
            {
                paused = true;
                stack.clear();
                active = controller.get(debugger, "activeMethod", 3).object();
                auto location = methodInfo(h, active);
                pausedPosition = controller.get(debugger, "currentSourcePos", 4).integer();
                location["position"] = pausedPosition;
                emit({{"event", "stopped"}, {"reason", nextStopReason}, {"method", location}});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.join();
        controller.invoke(debugger, "shutDown");
        if (!workerError.empty() && !stopping)
            emit({{"event", "error"}, {"message", workerError}});
        const int exitCode =
            stopping                                                                              ? 0
            : !workerError.empty()                                                                ? 1
            : config.value("unitTest", false) && returnValue.value("value", std::string{}) != "0" ? 1
                                                                                                  : 0;
        emit({{"event", "exited"}, {"exitCode", exitCode}, {"result", returnValue}});
        h.aborted = stopping;
        return exitCode;
    }
    catch (const std::exception &e)
    {
        emit({{"event", "error"}, {"message", e.what()}});
        return 2;
    }
}
