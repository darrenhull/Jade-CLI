#pragma once
// Jade Object Manager Guide, Development Environment Security:
// call the configured library's authentication and task hooks, never approximate them.
struct DevelopmentSecurity
{
    using Authenticate = int(JOMAPI *)(const Character *, const Character *);
    using Selected = int(JOMAPI *)(const Character *, const Character *, const Character *);
    HMODULE library{};
    Selected selected{};
    std::wstring user;
    ~DevelopmentSecurity()
    {
        if (library)
            FreeLibrary(library);
    }
    void open(const std::wstring &name, const std::filesystem::path &bin,
              const std::string &userName, const std::string &password)
    {
        if (name.empty() || !_wcsicmp(name.c_str(), L"<default>"))
            return;
        // Search the configured Jade binary directory, then normal DLL search paths,
        // matching Jade's documented library location. A missing library never permits access.
        auto location = std::filesystem::path(name);
        if (!location.is_absolute() && std::filesystem::exists(bin / location))
            location = bin / location;
        library = LoadLibraryExW(location.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library)
            throw NativeError(1711, "DevelopmentSecurityLibrary could not be loaded; inspection denied.");
        auto authenticate =
            reinterpret_cast<Authenticate>(GetProcAddress(library, "jadeDevelopmentUserInfo"));
        selected = reinterpret_cast<Selected>(GetProcAddress(library, "jadeDevelopmentFunctionSelected"));
        if (!authenticate || !selected)
            throw NativeError(1712,
                              "DevelopmentSecurityLibrary is missing required entry points "
                              "jadeDevelopmentUserInfo or jadeDevelopmentFunctionSelected.");
        user = wide(userName);
        auto secret = wide(password);
        const auto code = authenticate(user.c_str(), secret.c_str());
        SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));
        if (code)
            throw NativeError(1713, "Development security authentication denied these credentials.");
    }
    void authorize(const std::string &task, const std::string &entity)
    {
        if (selected && selected(user.c_str(), wide(task).c_str(), wide(entity).c_str()))
            throw NativeError(1713, "Development security policy denied " + task + " for " + entity + ".");
    }
};
