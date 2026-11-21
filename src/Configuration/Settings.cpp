#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Configuration/Settings.h>

SwiftSaveMenus::Settings SwiftSaveMenus::Settings::Load(const std::filesystem::path& path)
{
    const auto ini = std::filesystem::absolute(path);
    return { GetPrivateProfileIntW(L"General", L"EnableLogging", 0, ini.c_str()) == 1,
        GetPrivateProfileIntW(L"General", L"EnableCosavePruning", 1, ini.c_str()) == 1,
        GetPrivateProfileIntW(L"General", L"BackupPrunedCosaves", 0, ini.c_str()) == 1,
        GetPrivateProfileIntW(L"General", L"EnableSettingsMenu", 1, ini.c_str()) == 1 };
}

bool SwiftSaveMenus::Settings::Write(const std::filesystem::path& path, const wchar_t* key, bool value)
{
    std::error_code error;
    const auto ini = std::filesystem::absolute(path, error);
    return !error && WritePrivateProfileStringW(L"General", key, value ? L"1" : L"0", ini.c_str());
}
