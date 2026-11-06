#pragma once
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <stdexcept>
#include <iostream>
#include "json.hpp"
namespace jade {
using json = nlohmann::json;
namespace fs = std::filesystem;
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
fs::path filepath(const std::string& value);
std::string filename(const fs::path& value);
std::string read(const fs::path& path);
void write_json(const fs::path& path,const json& value);
json read_json(const fs::path& path);
std::string hash(const std::string& bytes);
std::string file_hash(const fs::path& path);
std::string canonical(const fs::path& path);
std::string lexical_key(const fs::path& path);
bool within(const fs::path& root,const fs::path& child);
struct Context {
    json request;
    fs::path install,storage;
    std::vector<fs::path> roots;
    std::string fingerprint;
    fs::path cache_path() const;
    fs::path index_path() const;
    explicit Context(const json& input);
};
json import_cache(Context& context);
json refresh_system_cache(Context& context);
json system_cache(Context& context);
json refresh_index(Context& context);
json resolve_type(Context& context,const std::string& schema,const std::string& name);
json native_operation(Context& context);
json native_access_test(Context& context);
json native_value_test(Context& context);
}
