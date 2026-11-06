#include "DdxMerger.hpp"
#include <xmllite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <set>
using Microsoft::WRL::ComPtr;
namespace jade::native {
static void check(HRESULT hr, const char *action) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(action) + " (HRESULT " + std::to_string(hr) + ").");
}
static std::wstring attribute(IXmlReader *reader, const wchar_t *name) {
    const wchar_t *value = nullptr;
    std::wstring result;
    if (reader->MoveToAttributeByName(name, nullptr) == S_OK) {
        check(reader->GetValue(&value, nullptr), "Read XML attribute");
        result = value ? value : L"";
        reader->MoveToElement();
    }
    return result;
}
std::string DdxMerger::merge(const std::string &schema,
                             const std::vector<std::filesystem::path> &files) {
    if (files.empty())
        return {};
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create XML output stream");
    ComPtr<IXmlWriter> writer;
    check(CreateXmlWriter(__uuidof(IXmlWriter), reinterpret_cast<void **>(writer.GetAddressOf()),
                          nullptr),
          "Create native XML writer");
    check(writer->SetOutput(stream.Get()), "Set XML output");
    check(writer->WriteStartDocument(XmlStandalone_Omit), "Write XML declaration");
    bool opened = false;
    std::map<std::wstring, std::wstring> expected;
    std::set<std::wstring> identities;
    for (auto &file : files) {
        auto bytes = readText(file);
        ComPtr<IStream> input;
        input.Attach(
            SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.data()), UINT(bytes.size())));
        if (!input)
            throw std::runtime_error("Cannot create XML input stream.");
        ComPtr<IXmlReader> reader;
        check(CreateXmlReader(__uuidof(IXmlReader),
                              reinterpret_cast<void **>(reader.GetAddressOf()), nullptr),
              "Create native XML reader");
        check(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit),
              "Disable XML DTDs");
        check(reader->SetInput(input.Get()), "Set XML input");
        XmlNodeType type;
        bool root = false;
        HRESULT hr;
        while ((hr = reader->Read(&type)) == S_OK) {
            UINT depth = 0;
            reader->GetDepth(&depth);
            const wchar_t *name = nullptr;
            reader->GetLocalName(&name, nullptr);
            if (type == XmlNodeType_Element && depth == 0) {
                if (root || !name || std::wstring(name) != L"schema" ||
                    attribute(reader.Get(), L"name") != wide(schema))
                    throw std::runtime_error("Invalid DDX schema identity: " +
                                             utf8(file.wstring()));
                root = true;
                std::map<std::wstring, std::wstring> attrs;
                const wchar_t *prefix = nullptr, *uri = nullptr;
                reader->GetPrefix(&prefix, nullptr);
                reader->GetNamespaceUri(&uri, nullptr);
                if (!opened)
                    check(writer->WriteStartElement(prefix, name, uri), "Write schema element");
                if (reader->MoveToFirstAttribute() == S_OK) {
                    do {
                        const wchar_t *local = nullptr, *value = nullptr, *aprefix = nullptr,
                                      *auri = nullptr;
                        reader->GetLocalName(&local, nullptr);
                        reader->GetValue(&value, nullptr);
                        reader->GetPrefix(&aprefix, nullptr);
                        reader->GetNamespaceUri(&auri, nullptr);
                        if (std::wstring(local) != L"CompleteDefinition")
                            attrs[std::wstring(aprefix ? aprefix : L"") + L":" + local] =
                                value ? value : L"";
                        if (!opened)
                            check(
                                writer->WriteAttributeString(
                                    aprefix, local, auri,
                                    std::wstring(local) == L"CompleteDefinition" ? L"true" : value),
                                "Write schema attribute");
                    } while (reader->MoveToNextAttribute() == S_OK);
                    reader->MoveToElement();
                }
                if (opened && attrs != expected)
                    throw std::runtime_error("Incompatible DDX schema headers: " + schema);
                if (!opened) {
                    expected = attrs;
                    opened = true;
                }
                continue;
            }
            if (!root)
                continue;
            if (type == XmlNodeType_EndElement && depth == 0)
                continue;
            if (depth == 1 && type == XmlNodeType_Element) {
                auto identity =
                    std::wstring(name ? name : L"") + L"::" + attribute(reader.Get(), L"name");
                if (!identities.insert(identity).second)
                    throw std::runtime_error("Duplicate DDX definition: " + utf8(identity));
                check(writer->WriteNode(reader.Get(), TRUE), "Write native DDX definition");
            }
        }
        check(hr, "Parse native DDX");
        if (!root)
            throw std::runtime_error("Missing DDX schema root: " + utf8(file.wstring()));
    }
    check(writer->WriteEndElement(), "Close schema element");
    check(writer->WriteEndDocument(), "Close XML document");
    check(writer->Flush(), "Flush XML output");
    STATSTG info{};
    check(stream->Stat(&info, STATFLAG_NONAME), "Read XML output length");
    if (info.cbSize.QuadPart > 32 * 1024 * 1024)
        throw std::runtime_error("Combined DDX exceeds size limit.");
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "Rewind XML output");
    std::string output(size_t(info.cbSize.QuadPart), 0);
    ULONG read = 0;
    check(stream->Read(output.data(), ULONG(output.size()), &read), "Read XML output");
    output.resize(read);
    return output;
}
} // namespace jade::native
