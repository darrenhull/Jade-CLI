#include "runtime.hpp"
#include "protocol.hpp"
#include "security.hpp"
#include "CatalogPage.hpp"
#include <jomoid.hpp>
ProtocolOutput protocolOutput;
void emit(json value)
{
    value["protocol"] = "jade-inspect/1";
    protocolOutput.write(value);
}
struct Inspector
{
    Runtime &r;
    DevelopmentSecurity security;
    bool production = false;
    int developmentLicences = 0;
    std::map<std::string, json> classCache;
    std::map<std::string, bool> filterCache;
    std::map<bool, CatalogPage> catalogPages;
    struct Cursor
    {
        Oid iterator{}, collection{}, classOid{};
        std::vector<Oid> classes;
        size_t nextClass = 0, index = 0;
        std::string binding;
        bool collectionMode = false;
        json buffered;
        unsigned keys = 0;
    };
    std::map<std::string, Cursor> cursors;
    unsigned nextCursor = 0;
    explicit Inspector(Runtime &runtime) : r(runtime)
    {
        production = r.get(r.instances[4], "getProductionMode", DSKBOOLEAN).value.body.boolean;
        Oid licence{};
        licence.set(DSKJADELICENCEINFO, 0);
        r.check(proc<decltype(&jomCreateObject)>(r.jom, "jomCreateObject")(
                    &r.process, &licence, CLASS_TRANSIENT, nullptr, nullptr, 0),
                "Read inspection licence policy");
        try
        {
            Param ignored, count(DSKINTEGER);
            r.send(licence, "getLicenceInfo", {}, ignored);
            r.property(licence, "developmentLicences", count);
            developmentLicences = count.value.body.integer;
        }
        catch (...)
        {
            r.destroy(licence);
            throw;
        }
        r.destroy(licence);
        Any library(r);
        Oid policyNode = r.instances[5];
        auto iniPath = r.config.at("iniPath").get<std::string>();
        if (r.config.value("server", "singleUser") == "multiUser")
        {
            // Read the database server node, never the untrusted client INI.
            // DbServerNodeOid is defined by the public JOM SDK.
            policyNode = DbServerNodeOid;
            Any serverIni(r);
            r.send(policyNode, "getIniFileName", {}, serverIni);
            iniPath = string(serverIni);
            if (iniPath.empty())
                throw std::runtime_error(
                    "Cannot verify the database server inspection policy: server INI is unavailable.");
        }
        auto file = Param::string(iniPath), section = Param::string("JadeSecurity"),
             key = Param::string("DevelopmentSecurityLibrary"), fallback = Param::string("");
        r.send(policyNode, "getProfileString", {&file, &section, &key, &fallback}, library);
        security.open(wide(string(library)), std::filesystem::path(wide(r.config.at("installPath"))) / L"bin",
                      r.config.value("user", ""), r.config.value("password", ""));
    }
    Oid classOf(const Oid &object)
    {
        Meta instance(r, "DskObject", &object);
        return instance.reference("?getClass@DskObject@@UEBAHAEAVDskClass@@I@Z", "DskClass");
    }
    bool kindOf(const Oid &oid, unsigned number)
    {
        unsigned result{};
        r.check(proc<decltype(&jomIsKindOf)>(r.jom, "jomIsKindOf")(&r.process, &oid, number, &result, 0),
                "Inspect object kind");
        return result != 0;
    }
    std::string schemaName(const Oid &cls)
    {
        Meta type(r, "DskClass", &cls);
        auto oid = type.reference("?getSchema@DskType@@UEBAHAEAVDskSchema@@@Z", "DskSchema");
        Meta schema(r, "DskSchema", &oid);
        return schema.name();
    }
    json classInfo(const Oid &oid)
    {
        const auto id = hex(&oid, 22);
        if (classCache.contains(id))
            return classCache.at(id);
        Meta cls(r, "DskClass", &oid);
        auto parent = cls.reference("?getSuperclass@DskClass@@QEBAHAEAV1@@Z", "DskClass");
        std::string parentName;
        if (!nullOid(parent))
        {
            Meta p(r, "DskClass", &parent);
            parentName = p.name();
        }
        bool collection = false;
        std::set<std::string> seen;
        auto current = oid;
        while (!nullOid(current) && seen.insert(hex(&current, 22)).second)
        {
            Meta c(r, "DskClass", &current);
            if (c.scalar<unsigned>("?getNumber@DskSchemaEntity@@UEBAHPEAII@Z") == DSKCOLLECTION)
            {
                collection = true;
                break;
            }
            current = c.reference("?getSuperclass@DskClass@@QEBAHAEAV1@@Z", "DskClass");
        }
        auto result =
            json{{"oid", hex(&oid, 22)},
                 {"name", cls.name()},
                 {"schema", schemaName(oid)},
                 {"superclass", parentName},
                 {"persistentAllowed", cls.scalar<bool>("?getPersistentAllowed@DskType@@UEBAHPEA_NI@Z")},
                 {"collection", collection}};
        classCache[id] = result;
        return result;
    }
    json catalog(bool includeSystem)
    {
        classCache.clear();
        filterCache.clear();
        json schemas = json::array();
        std::vector<Oid> pending{r.instances[2]};
        std::set<std::string> seen;
        for (size_t i = 0; i < pending.size(); i++)
        {
            auto id = pending[i];
            if (!seen.insert(hex(&id, 22)).second)
                continue;
            Meta schema(r, "DskSchema", &id);
            for (const auto &child : schema.members("?subschemas@DskSchema@@UEBAHAEAVJomColl@@@Z"))
                pending.push_back(child);
            auto name = schema.name();
            if (!includeSystem &&
                (name == "RootSchema" || name == "JadeSchema" || name.rfind("Jade", 0) == 0))
                continue;
            json classes = json::array();
            for (const auto &cls : schema.members("?classes@DskSchema@@UEBAHAEAVJomColl@@@Z"))
                classes.push_back(classInfo(cls));
            std::sort(classes.begin(), classes.end(),
                      [](const json &a, const json &b) { return a["name"] < b["name"]; });
            schemas.push_back({{"name", name}, {"oid", hex(&id, 22)}, {"classes", classes}});
        }
        std::sort(schemas.begin(), schemas.end(),
                  [](const json &a, const json &b) { return a["name"] < b["name"]; });
        return {{"schemas", schemas}, {"readOnly", true}};
    }
    json catalogRequest(const json &request)
    {
        const auto includeSystem = request.value("includeSystem", false);
        if (!CatalogPage::requested(request))
        {
            // Existing local callers still receive the complete schema tree.
            catalogPages.erase(includeSystem);
            return catalog(includeSystem);
        }
        CatalogPage::validate(request);
        if (request.value("refresh", false))
            catalogPages.clear();
        auto page = catalogPages.find(includeSystem);
        if (page == catalogPages.end())
            page = catalogPages.emplace(includeSystem, CatalogPage(catalog(includeSystem))).first;
        return page->second.page(request);
    }
    std::string string(Param &value)
    {
        auto format = value.value.header.format;
        if (format == DSKCSTRING)
            return utf8(value.value.body.pText ? value.value.body.pText : L"");
        if (format == DSKJADESTRING)
        {
            Size count{};
            if (JADE_STRING_LENGTH(&value.value.body.dataHandler, count))
                throw std::runtime_error("Cannot read native String length.");
            if (count > 4096)
                return "<String exceeds 4096-character preview>";
            std::wstring text(count + 1, 0);
            if (JADE_GET_STRING(&value.value.body.dataHandler, text.data(), count + 1))
                throw std::runtime_error("Cannot read native String.");
            text.resize(count);
            return utf8(text);
        }
        if (format == DSKPSTRING)
        {
            if (value.value.body.string.length > 4096)
                return "<String exceeds 4096-character preview>";
            return utf8(std::wstring(value.value.body.string.dataArea, value.value.body.string.length));
        }
        if (format == DSKCHARBLOCKPOINTER)
        {
            if (value.value.body.charBlockPtr.length > 4096)
                return "<String exceeds 4096-character preview>";
            return utf8(
                std::wstring(value.value.body.charBlockPtr.charPtr + value.value.body.charBlockPtr.offset,
                             value.value.body.charBlockPtr.length));
        }
        if (format == DSKJADEBINARY)
        {
            Size length{};
            r.check(JADE_BINARY_LENGTH(&value.value.body.dataHandler, length), "Read native binary size");
            if (length > 4096)
                return "<Binary exceeds 4096-byte preview>";
        }
        if (format == DSKJADESTRINGUTF8)
        {
            Size length{};
            r.check(JADE_STRINGUTF8_LENGTH(&value.value.body.dataHandler, length), "Read native UTF-8 size");
            if (length > 4096)
                return "<StringUtf8 exceeds 4096-character preview>";
        }
        Any text(r);
        auto selector = Param::string("asString");
        r.check(proc<decltype(&jomCallPrimMethod)>(r.jom, "jomCallPrimMethod")(
                    &r.process, DSKANY_PRIMITIVE, &value.value, &selector.value, nullptr, &text.value, 0),
                "Format native primitive");
        return string(text);
    }
    json display(Param &value, const std::string &declaredType = "")
    {
        auto format = value.value.header.format;
        if (format == DSKOBJECTID || format == DSKSHORTOBJECTID)
        {
            auto oid = value.object();
            if (nullOid(oid))
                return {{"value", "null"},
                        {"type", declaredType.empty() ? "Object" : declaredType},
                        {"oid", nullptr}};
            return {{"value", hex(&oid, 22)},
                    {"type", declaredType.empty() ? "Object" : declaredType},
                    {"oid", hex(&oid, 22)}};
        }
        if (format == DSKNULL || format == DSKUNDEF)
            return {
                {"value", "null"}, {"type", declaredType.empty() ? "Any" : declaredType}, {"oid", nullptr}};
        return {{"value", string(value)},
                {"type", declaredType.empty() ? "Primitive" : declaredType},
                {"format", static_cast<unsigned>(format)}};
    }
    void authorize(const Oid &object, const Oid &classOid)
    {
        auto info = classInfo(classOid);
        security.authorize("inspectInstances", info.at("schema").get<std::string>() + "::" + info.at("name").get<std::string>());
        // Application-defined inspection filters are invoked on the actual receiver.
        // No synthetic instance is created and no application getter is evaluated.
        const auto id = hex(&classOid, 22);
        if (!filterCache.contains(id))
        {
            bool hasFilter = false;
            auto current = classOid;
            std::set<std::string> seen;
            while (!nullOid(current) && seen.insert(hex(&current, 22)).second)
            {
                Meta cls(r, "DskClass", &current);
                for (const auto &oid : cls.members("?methods@DskType@@UEBAHAEAVJomColl@@@Z"))
                {
                    Meta method(r, "DskMethod", &oid);
                    if (method.name() == "allowedToInspect")
                    {
                        hasFilter = true;
                        break;
                    }
                }
                if (hasFilter)
                    break;
                current = cls.reference("?getSuperclass@DskClass@@QEBAHAEAV1@@Z", "DskClass");
            }
            filterCache[id] = hasFilter;
        }
        if ((production || developmentLicences <= 0) && !filterCache.at(id) && !security.library)
            throw std::runtime_error(
                "Production or runtime-only inspection requires allowedToInspect or a development "
                "security library permitting this class.");
        if (filterCache.at(id))
        {
            auto user = Param::string(r.config.value("user", "")),
                 password = Param::string(r.config.value("password", ""));
            Param allowed(DSKBOOLEAN);
            r.send(object, "allowedToInspect", {&user, &password}, allowed);
            if (!allowed.value.body.boolean)
                throw std::runtime_error("Application inspection policy denied this object.");
        }
    }
    json object(const Oid &oid)
    {
        bool valid{};
        r.check(proc<decltype(&jomIsValidObject)>(r.jom, "jomIsValidObject")(&r.process, &oid, valid, 0),
                "Validate inspected object");
        if (!valid)
            throw std::runtime_error("Object is missing or has been deleted.");
        auto cls = classOf(oid);
        authorize(oid, cls);
        auto info = classInfo(cls);
        json properties = json::array();
        auto all = r.get(cls, "allProperties").object(), iter = r.get(all, "createIterator").object();
        try
        {
            for (unsigned count = 0; count < 4096; count++)
            {
                Param field(DSKOBJECTID), more(DSKBOOLEAN);
                r.send(iter, "next", {&field}, more);
                if (!more.value.body.boolean)
                    break;
                auto id = field.object();
                Meta p(r, "DskProperty", &id);
                if (p.scalar<AccessType>("?getSubAccess@DskSchemaEntity@@UEBAHPEAW4AccessType@@@Z") >=
                    SUBACCESS_UNPUBLISHED)
                    continue;
                const auto name = p.name();
                auto typeOid = p.reference("?getType@DskFeature@@UEBAHAEAVDskType@@@Z", "DskType");
                Meta type(r, "DskType", &typeOid);
                auto typeName = type.name();
                json item = {{"name", name}, {"type", typeName}};
                try
                {
                    if (p.scalar<bool>("?isMapped@DskProperty@@UEBAHPEA_N@Z"))
                        throw std::runtime_error("Mapped property not evaluated by read-only inspection.");
                    Any value(r);
                    r.property(oid, name, value);
                    auto description = display(value, typeName);
                    item.update(description);
                }
                catch (const std::exception &e)
                {
                    item["value"] = "<unavailable>";
                    item["error"] = e.what();
                }
                properties.push_back(item);
            }
        }
        catch (...)
        {
            r.destroy(iter);
            throw;
        }
        r.destroy(iter);
        return {{"oid", hex(&oid, 22)},     {"className", info["name"]},
                {"schema", info["schema"]}, {"collection", info["collection"]},
                {"properties", properties}, {"readOnly", true}};
    }
    json identity(const Oid &oid)
    {
        auto cls = classOf(oid);
        authorize(oid, cls);
        Meta type(r, "DskClass", &cls);
        const auto name = type.name();
        return {{"oid", hex(&oid, 22)},
                {"className", name},
                {"schema", schemaName(cls)},
                {"label", name + " · " + hex(&oid, 22)}};
    }
    void release(Cursor &c)
    {
        if (!nullOid(c.iterator))
        {
            r.destroy(c.iterator);
            c.iterator = {};
        }
    }
    ~Inspector()
    {
        for (auto &[id, c] : cursors)
            try
            {
                release(c);
            }
            catch (...)
            {
            }
    }
    void setCollection(Cursor &c, const Oid &collection)
    {
        c.collection = collection;
        c.iterator = r.get(collection, "createIterator").object();
        c.keys = 0;
        if (kindOf(collection, DSKDICTIONARY))
        {
            auto cls = classOf(collection);
            Meta type(r, "DskCollClass", &cls);
            c.keys = static_cast<unsigned>(type.members("?keys@DskCollClass@@UEBAHAEAVJomColl@@@Z").size());
        }
    }
    json next(Cursor &c)
    {
        while (true)
        {
            if (nullOid(c.iterator))
            {
                if (c.nextClass >= c.classes.size())
                    return nullptr;
                auto cls = c.classes[c.nextClass++];
                Param coll(DSKOBJECTID);
                r.property(cls, "instances", coll);
                setCollection(c, coll.object());
            }
            Any value(r);
            Param more(DSKBOOLEAN);
            r.send(c.iterator, "next", {&value}, more);
            if (!more.value.body.boolean)
            {
                release(c);
                if (c.collectionMode)
                    return nullptr;
                continue;
            }
            if (!c.collectionMode)
            {
                auto oid = value.object();
                try
                {
                    return identity(oid);
                }
                catch (const NativeError &error)
                {
                    if (error.code == 4)
                        continue;
                    throw;
                }
            }
            auto item = display(value);
            item["index"] = ++c.index;
            if (c.keys)
            {
                json keys = json::array();
                for (unsigned i = 1; i <= c.keys; i++)
                {
                    auto ordinal = Param::integer(i);
                    Any key(r);
                    r.send(c.iterator, "getCurrentKey", {&ordinal}, key);
                    keys.push_back(display(key));
                }
                item["keys"] = keys;
                std::string label;
                for (const auto &key : keys)
                {
                    if (!label.empty())
                        label += " · ";
                    label += key.at("value").get<std::string>();
                }
                item["key"] = label;
            }
            return item;
        }
    }
    json page(const json &request, bool collectionMode)
    {
        const auto limit = request.value("limit", request.value("pageSize", 50));
        if (limit < 1 || limit > 100)
            throw std::runtime_error("Page limit must be between 1 and 100.");
        const auto source = request.at(collectionMode ? "oid" : "classOid").get<std::string>();
        const auto sourceOid = parseOid(source);
        bool valid = false;
        if (nullOid(sourceOid))
            throw std::runtime_error("Inspection OID cannot be null.");
        r.check(
            proc<decltype(&jomIsValidObject)>(r.jom, "jomIsValidObject")(&r.process, &sourceOid, valid, 0),
            "Validate inspection query");
        if (!valid)
            throw std::runtime_error("Inspection object is missing or has been deleted.");
        if (!collectionMode && !kindOf(sourceOid, DSKCLASS))
            throw std::runtime_error("Instance query requires a class OID.");
        if (collectionMode)
        {
            if (!kindOf(sourceOid, DSKCOLLECTION))
                throw std::runtime_error("Object is not a collection.");
            authorize(sourceOid, classOf(sourceOid));
        }
        else
        {
            auto info = classInfo(sourceOid);
            security.authorize(request.value("includeSubclasses", false) ? "inspectAllInstances" : "inspectInstances",
                               info.at("schema").get<std::string>() + "::" + info.at("name").get<std::string>());
        }
        auto token = request.contains("cursor") && !request["cursor"].is_null()
                         ? request.at("cursor").get<std::string>()
                         : std::string{};
        if (token.empty())
        {
            if (cursors.size() >= 32)
                throw std::runtime_error(
                    "Too many open inspection cursors; refresh or release a previous page.");
            Cursor c;
            c.binding = source;
            c.collectionMode = collectionMode;
            if (collectionMode)
            {
                setCollection(c, sourceOid);
            }
            else
            {
                c.classes.push_back(sourceOid);
                if (request.value("includeSubclasses", false))
                {
                    std::set<std::string> seen{source};
                    for (size_t i = 0; i < c.classes.size(); i++)
                    {
                        Meta cls(r, "DskClass", &c.classes[i]);
                        for (const auto &child : cls.members("?subclasses@DskClass@@UEBAHAEAVJomColl@@@Z"))
                            if (seen.insert(hex(&child, 22)).second)
                                c.classes.push_back(child);
                    }
                }
            }
            token = "cursor-" + std::to_string(++nextCursor);
            cursors.emplace(token, std::move(c));
        }
        auto found = cursors.find(token);
        if (found == cursors.end() || found->second.binding != source ||
            found->second.collectionMode != collectionMode)
            throw std::runtime_error("Inspection cursor is invalid or belongs to another query.");
        auto &cursor = found->second;
        json items = json::array();
        try
        {
            if (!cursor.buffered.is_null())
            {
                if (!collectionMode)
                {
                    auto bufferedOid = parseOid(cursor.buffered.at("oid"));
                    authorize(bufferedOid, classOf(bufferedOid));
                }
                items.push_back(cursor.buffered);
                cursor.buffered = nullptr;
            }
            while (items.size() < size_t(limit))
            {
                auto item = next(cursor);
                if (item.is_null())
                    break;
                items.push_back(item);
            }
            cursor.buffered = next(cursor);
            const bool more = !cursor.buffered.is_null();
            json result = {{"items", items},
                           {"cursor", more ? json(token) : json(nullptr)},
                           {"hasMore", more},
                           {"readOnly", true}};
            if (collectionMode)
                result["oid"] = source;
            if (!more)
            {
                release(cursor);
                cursors.erase(found);
            }
            return result;
        }
        catch (...)
        {
            release(cursor);
            cursors.erase(found);
            throw;
        }
    }
    json dispatch(const json &request)
    {
        const auto command = request.value("command", "");
        if (command == "catalog")
            return catalogRequest(request);
        if (command == "object")
            return object(parseOid(request.at("oid")));
        if (command == "instances")
            return page(request, false);
        if (command == "collection")
            return page(request, true);
        if (command == "release")
        {
            auto found = cursors.find(request.at("cursor"));
            if (found != cursors.end())
            {
                release(found->second);
                cursors.erase(found);
            }
            return {{"released", true}};
        }
        throw std::runtime_error("Unknown inspection command: " + command);
    }
};
int wmain(int argc, wchar_t **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try
    {
        protocolOutput.initialize();
        if (argc != 2)
            throw std::runtime_error("Expected native inspection session JSON file.");
        std::ifstream stream{std::filesystem::path(argv[1])};
        json config;
        stream >> config;
        Runtime runtime(config);
        Inspector inspector(runtime);
        emit({{"event", "ready"},
              {"readOnly", true},
              {"security", {{"developmentLibrary", inspector.security.library != nullptr}, {"production", inspector.production}, {"developmentLicences", inspector.developmentLicences}}},
              {"databasePath", config.at("databasePath")},
              {"server", config.value("server", "singleUser")},
              {"schema", "RootSchema"},
              {"requestedSchema", config.value("schema", "RootSchema")}});
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.size() > 1024 * 1024)
                throw std::runtime_error("Inspection request exceeds size limit.");
            json request;
            try
            {
                request = json::parse(line);
                if (!request.is_object() || !request.contains("id") || !request["id"].is_number_integer())
                    throw std::runtime_error("Invalid inspection request identifier.");
                if (request.value("command", "") == "close")
                {
                    emit({{"id", request["id"]}, {"success", true}, {"body", {{"closed", true}}}});
                    break;
                }
                emit({{"id", request["id"]}, {"success", true}, {"body", inspector.dispatch(request)}});
            }
            catch (const NativeError &e)
            {
                if (protocolOutput.broken)
                    return 2;
                emit({{"id", request.is_object() && request.contains("id") ? request["id"] : json(0)},
                      {"success", false},
                      {"message", e.what()},
                      {"code", e.code}});
            }
            catch (const std::exception &e)
            {
                if (protocolOutput.broken)
                    return 2;
                emit({{"id", request.is_object() && request.contains("id") ? request["id"] : json(0)},
                      {"success", false},
                      {"message", e.what()}});
            }
        }
        return 0;
    }
    catch (const NativeError &e)
    {
        emit({{"event", "error"}, {"message", e.what()}, {"code", e.code}});
        return 2;
    }
    catch (const std::exception &e)
    {
        emit({{"event", "error"}, {"message", e.what()}});
        return 2;
    }
}
