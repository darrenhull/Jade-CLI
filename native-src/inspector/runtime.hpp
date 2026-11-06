#pragma once
#define NOMINMAX
#include <windows.h>
#define DSKPARAM_LINKAGE
#include <jomcalls.h>
#include <jomparam.hpp>
#include <json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
using json = nlohmann::json;
using Oid = DskObjectId;
template <class T> T proc(HMODULE module, const char *name)
{
    auto p = GetProcAddress(module, name);
    if (!p)
        throw std::runtime_error(std::string("Native export unavailable: ") + name);
    return reinterpret_cast<T>(p);
}
std::wstring wide(const std::string &text)
{
    if (text.empty())
        return {};
    const auto count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring value(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), value.data(), count);
    return value;
}
std::string utf8(const std::wstring &text)
{
    if (text.empty())
        return {};
    const auto count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string value(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), value.data(), count, nullptr, nullptr);
    return value;
}
std::string hex(const void *data, size_t size)
{
    static const char *chars = "0123456789abcdef";
    std::string value;
    for (size_t i = 0; i < size; i++)
    {
        const auto c = static_cast<const unsigned char *>(data)[i];
        value += chars[c >> 4];
        value += chars[c & 15];
    }
    return value;
}
Oid parseOid(const std::string &text)
{
    if (text.size() != 44 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw std::runtime_error("OID must be 44 hexadecimal characters.");
    Oid value{};
    auto out = reinterpret_cast<unsigned char *>(&value);
    for (size_t i = 0; i < 22; i++)
        out[i] = static_cast<unsigned char>(std::stoul(text.substr(i * 2, 2), nullptr, 16));
    return value;
}
bool nullOid(const Oid &oid)
{
    return std::all_of(reinterpret_cast<const unsigned char *>(&oid),
                       reinterpret_cast<const unsigned char *>(&oid) + 8, [](auto c) { return c == 0; });
}
struct Param
{
    DskParam value{};
    std::shared_ptr<std::wstring> text;
    explicit Param(PFormat format = DSKUNDEF)
    {
        value.header.format = format;
    }
    static Param string(const std::string &value)
    {
        Param p(DSKCSTRING);
        p.text = std::make_shared<std::wstring>(wide(value));
        p.value.body.pText = const_cast<wchar_t *>(p.text->c_str());
        return p;
    }
    static Param integer(int value)
    {
        Param p(DSKINTEGER);
        p.value.body.integer = value;
        return p;
    }
    static Param boolean(bool value)
    {
        Param p(DSKBOOLEAN);
        p.value.body.boolean = value;
        return p;
    }
    static Param oid(const Oid &value)
    {
        Param p(DSKOBJECTID);
        p.value.body.oid = value;
        return p;
    }
    Oid object() const
    {
        Oid result{};
        if (paramGetOid(value, result))
            throw std::runtime_error("Expected native object reference.");
        return result;
    }
};
struct NativeError : std::runtime_error
{
    int code;
    NativeError(int n, const std::string &message) : std::runtime_error(message), code(n)
    {
    }
};
struct Runtime
{
    HMODULE jom{}, util{};
    DskHandle node{}, process{}, security{};
    std::array<Oid, 8> instances{};
    bool initialized = false, signedOn = false;
    json config;
    explicit Runtime(const json &settings, bool fixture = false) : config(settings)
    {
        auto bin = std::filesystem::path(wide(config.at("installPath"))) / L"bin";
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        AddDllDirectory(bin.c_str());
        auto load = [&](const wchar_t *name) {
            auto m = LoadLibraryExW((bin / name).c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!m)
                throw std::runtime_error("Cannot load native Jade runtime.");
            return m;
        };
        jom = load(L"jom.dll");
        util = load(L"jomutil.dll");
        try
        {
            auto db = Param::string(config.at("databasePath")), ini = Param::string(config.at("iniPath")),
                 server = Param::string(config.value("server", "singleUser"));
            if (server.text->compare(L"singleUser") && server.text->compare(L"multiUser"))
                throw std::runtime_error("Invalid Jade server mode.");
            check(proc<decltype(&jomInitialize)>(jom, "jomInitialize")(&node, &db.value, &server.value,
                                                                       &ini.value),
                  "Open database");
            initialized = true;
            auto schema = Param::string(fixture ? config.value("schema", "RootSchema") : "RootSchema"),
                 app = Param::string("RootSchemaApp"), user = Param::string(config.value("user", "")),
                 password = Param::string(config.value("password", "")), mode = Param::integer(0),
                 usage = Param::integer(fixture ? 0 : 1);
            check(proc<decltype(&jomSignOn)>(jom, "jomSignOn")(&node, &process, &security, &schema.value,
                                                               &app.value, &user.value, &password.value,
                                                               &mode.value, &usage.value, 0),
                  "Read-only sign-on");
            signedOn = true;
            check(proc<decltype(&jomGetExecInstances)>(jom, "jomGetExecInstances")(
                      &process, &instances[0], &instances[1], &instances[2], &instances[3], &instances[4],
                      &instances[5], &instances[6], &instances[7], 0),
                  "Get schema context");
        }
        catch (...)
        {
            close();
            throw;
        }
    }
    void check(int code, const char *operation)
    {
        if (!code)
            return;
        wchar_t message[2048]{};
        proc<int (*)(int, wchar_t *, unsigned)>(util, "jomGetMessageText")(code, message, 2048);
        throw NativeError(code,
                          std::string(operation) + ": Jade " + std::to_string(code) + " " + utf8(message));
    }
    void close()
    {
        if (signedOn)
        {
            proc<decltype(&jomSignOff)>(jom, "jomSignOff")(&process, 0);
            signedOn = false;
        }
        if (initialized)
        {
            proc<decltype(&jomFinalize)>(jom, "jomFinalize")(&node);
            initialized = false;
        }
    }
    ~Runtime()
    {
        close();
    }
    void send(const Oid &oid, const std::string &name, const std::vector<Param *> &args, Param &out)
    {
        auto selector = Param::string(name);
        Param list(DSKPARAMLIST);
        list.value.body.parameterList.entries = static_cast<EntryCount>(args.size());
        list.value.body.parameterList.size = static_cast<Size>(args.size() * sizeof(DskParam *));
        for (size_t i = 0; i < args.size(); i++)
            list.value.body.parameterList.param[i] = &args[i]->value;
        auto input = args.empty() ? nullptr : args.size() == 1 ? &args[0]->value : &list.value;
        check(proc<decltype(&jomSendMsg)>(jom, "jomSendMsg")(&process, &oid, &selector.value, input,
                                                             &out.value, 0),
              name.c_str());
    }
    Param get(const Oid &oid, const std::string &name, PFormat format = DSKOBJECTID)
    {
        Param out(format);
        send(oid, name, {}, out);
        return out;
    }
    void property(const Oid &oid, const std::string &name, Param &out)
    {
        auto field = Param::string(name);
        check(proc<decltype(&jomGetProperty)>(jom, "jomGetProperty")(&process, &oid, &field.value, &out.value,
                                                                     0),
              name.c_str());
    }
    void destroy(const Oid &oid)
    {
        if (!nullOid(oid))
            check(
                proc<decltype(&jomDeleteObject)>(jom, "jomDeleteObject")(&process, &oid, nullptr, nullptr, 0),
                "Release native iterator");
    }
};
struct Any : Param
{
    Runtime &runtime;
    explicit Any(Runtime &r) : runtime(r)
    {
        runtime.check(proc<int (*)(DskParam *, unsigned char)>(runtime.util, "paramCreateJadeAny")(&value, 3),
                      "Allocate native value");
    }
    ~Any()
    {
        proc<int (*)(DskParam *)>(runtime.util, "paramDeleteJadeAny")(&value);
    }
    Any(const Any &) = delete;
};
struct Meta
{
    Runtime &runtime;
    std::string kind;
    alignas(8) unsigned char data[40]{};
    Meta(Runtime &r, const char *type, const Oid *oid = nullptr) : runtime(r), kind(type)
    {
        proc<void *(*)(void *)>(runtime.jom, ("??0" + kind + "@@QEAA@XZ").c_str())(data);
        if (oid)
            memcpy(data + 16, oid, 22);
    }
    ~Meta()
    {
        proc<void (*)(void *)>(runtime.jom, ("??1" + kind + "@@UEAA@XZ").c_str())(data);
    }
    Oid oid() const
    {
        Oid value{};
        memcpy(&value, data + 16, 22);
        return value;
    }
    std::string name()
    {
        wchar_t value[256]{};
        if (kind == "DskSchema")
            runtime.check(proc<int (*)(void *, wchar_t *)>(runtime.jom,
                                                           "?getName@DskSchema@@UEBAHQEA_W@Z")(data, value),
                          "Read schema name");
        else
            runtime.check(proc<int (*)(void *, wchar_t *, unsigned)>(
                              runtime.jom, "?getName@DskSchemaEntity@@UEBAHQEA_WI@Z")(data, value, 0),
                          "Read metadata name");
        return utf8(value);
    }
    template <class T> T scalar(const char *symbol)
    {
        T value{};
        runtime.check(proc<int (*)(void *, T *, unsigned)>(runtime.jom, symbol)(data, &value, 0), symbol);
        return value;
    }
    Oid reference(const char *symbol, const char *target)
    {
        Meta out(runtime, target);
        runtime.check(proc<int (*)(void *, void *)>(runtime.jom, symbol)(data, out.data), symbol);
        return out.oid();
    }
    std::vector<Oid> members(const char *symbol)
    {
        alignas(8) unsigned char storage[56]{};
        proc<void *(*)(void *, unsigned)>(runtime.jom, "??0OrderedJomColl@@QEAA@I@Z")(storage, 0);
        struct Guard
        {
            Runtime &r;
            void *p;
            ~Guard()
            {
                proc<void (*)(void *)>(r.jom, "??1OrderedJomColl@@UEAA@XZ")(p);
            }
        } guard{runtime, storage};
        runtime.check(proc<int (*)(void *, void *)>(runtime.jom, symbol)(data, storage), symbol);
        const auto count = proc<unsigned (*)(void *)>(runtime.jom, "?size@OrderedJomColl@@UEBAIXZ")(storage);
        std::vector<Oid> result;
        for (unsigned i = 1; i <= count; i++)
        {
            auto item = proc<void *(*)(void *, unsigned)>(
                runtime.jom, "?at@OrderedJomColl@@QEBAPEAVJomObj@@I@Z")(storage, i);
            Oid oid{};
            memcpy(&oid, static_cast<unsigned char *>(item) + 16, 22);
            result.push_back(oid);
        }
        return result;
    }
};
