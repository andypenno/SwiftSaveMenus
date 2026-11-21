#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Search/PortableProfile.h>

namespace SwiftSaveMenus
{
    std::wstring UnderlyingDirectory(std::wstring_view directory)
    {
        const auto separator = directory.find_last_of(L"\\/");
        if (separator == std::wstring_view::npos) {
            return {};
        }
        using UnixPath = char* (__cdecl*)(const wchar_t*);
        const auto unixPath = reinterpret_cast<UnixPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"));
        if (!unixPath) {
            return {};
        }
        const auto parent = std::wstring(directory.substr(0, separator));
        auto* unixParent = unixPath(parent.c_str());
        if (!unixParent) {
            return {};
        }
        wchar_t wideParent[MAX_PATH];
        const auto converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, unixParent, -1, wideParent, MAX_PATH);
        HeapFree(GetProcessHeap(), 0, unixParent);
        if (!converted || wideParent[0] != L'/') {
            return {};
        }
        for (auto* position = wideParent; *position; ++position) {
            if (*position == L'/') {
                *position = L'\\';
            }
        }
        // The Unix alias reaches the backing folder without MO2's drive-letter mappings.
        return std::wstring(L"\\??\\unix") + wideParent + L"\\__MO_Saves";
    }

    std::wstring PortableProfile(std::wstring_view virtualDirectory)
    {
        const auto separator = virtualDirectory.find_last_of(L"\\/");
        const auto leaf = virtualDirectory.substr(separator == std::wstring_view::npos ? 0 : separator + 1);
        if (_wcsicmp(std::wstring(leaf).c_str(), L"__MO_Saves") != 0) {
            return {};
        }
        const auto usvfs = GetModuleHandleW(L"usvfs_x64.dll");
        wchar_t modulePath[MAX_PATH];
        const auto moduleLength = usvfs ? GetModuleFileNameW(usvfs, modulePath, MAX_PATH) : 0;
        if (moduleLength == 0 || moduleLength >= MAX_PATH) {
            return {};
        }
        std::wstring root(modulePath);
        root.resize(root.find_last_of(L"\\/") + 1);
        const auto ini = root + L"ModOrganizer.ini";
        if (GetFileAttributesW((root + L"ModOrganizer.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
            return {};
        }
        wchar_t value[MAX_PATH];
        GetPrivateProfileStringW(L"General", L"gameName", L"", value, MAX_PATH, ini.c_str());
        const wchar_t* gameIni{};
        if (_wcsicmp(value, L"Skyrim Special Edition") == 0) {
            gameIni = L"skyrimcustom.ini";
        } else if (_wcsicmp(value, L"Skyrim VR") == 0) {
            gameIni = L"skyrimvr.ini";
        } else {
            return {};
        }
        for (const auto* key : { L"base_directory", L"profiles_directory" }) {
            if (GetPrivateProfileStringW(L"Settings", key, L"", value, MAX_PATH, ini.c_str())) {
                return {};
            }
        }
        const auto length = GetPrivateProfileStringW(L"General", L"selected_profile", L"", value, MAX_PATH, ini.c_str());
        std::wstring_view selected(value, length);
        constexpr std::wstring_view prefix = L"@ByteArray(";
        if (length == MAX_PATH - 1 || !selected.starts_with(prefix) || !selected.ends_with(L')')) {
            return {};
        }
        selected.remove_prefix(prefix.size());
        selected.remove_suffix(1);
        if (selected.empty() ||
            selected.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-") != std::wstring_view::npos) {
            return {};
        }
        const auto profile = root + L"profiles\\" + std::wstring(selected) + L"\\";
        GetPrivateProfileStringW(L"General", L"LocalSaves", L"", value, MAX_PATH, (profile + L"settings.ini").c_str());
        if (_wcsicmp(value, L"true") != 0) {
            return {};
        }
        GetPrivateProfileStringW(L"General", L"sLocalSavePath", L"", value, MAX_PATH, (profile + gameIni).c_str());
        return _wcsicmp(value, L"__MO_Saves\\") == 0 ? profile + L"saves" : std::wstring{};
    }

    bool SameSave(std::wstring_view physical, const char* virtualDirectory, const char* filename)
    {
        WCHAR wideName[MAX_PATH];
        const auto codePage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
        if (!MultiByteToWideChar(codePage, 0, filename, -1, wideName, MAX_PATH)) {
            return false;
        }
        const auto flags = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        const auto source = CreateFileW((std::wstring(physical) + L"\\" + wideName).c_str(),
            FILE_READ_ATTRIBUTES, flags, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto target = CreateFileA((std::string(virtualDirectory) + "\\" + filename).c_str(),
            FILE_READ_ATTRIBUTES, flags, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        BY_HANDLE_FILE_INFORMATION a{}, b{};
        const bool same = source != INVALID_HANDLE_VALUE && target != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(source, &a) && GetFileInformationByHandle(target, &b) &&
            a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
            a.nFileIndexLow == b.nFileIndexLow && (a.nFileIndexHigh || a.nFileIndexLow);
        if (source != INVALID_HANDLE_VALUE) {
            CloseHandle(source);
        }
        if (target != INVALID_HANDLE_VALUE) {
            CloseHandle(target);
        }
        return same;
    }
}
