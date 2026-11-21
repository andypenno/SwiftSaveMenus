#pragma once

#include <filesystem>

namespace SaveLoadFix
{
    struct Settings
    {
        inline static constexpr auto path = L"Data/SKSE/Plugins/SaveLoadFix.ini";
        bool enableLogging{};
        bool enableCosavePruning{ true };
        bool backupPrunedCosaves{};
        bool enableSettingsMenu{ true };

        static Settings Load(const std::filesystem::path& path);
        static bool Write(const std::filesystem::path& path, const wchar_t* key, bool value);
    };
}
