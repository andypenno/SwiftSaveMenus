#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Configuration/DiagnosticLogging.h>
#include <Configuration/Settings.h>
#include <Cosaves/StartupPruning.h>
#include <Menu/MenuTiming.h>
#include <Menu/SettingsMenu.h>

namespace
{
    using Render = void (__stdcall*)();
    using AddSectionItem = void (*)(const char*, Render);
    using Checkbox = bool (*)(const char*, bool*);
    using Text = void (*)(const char*, const char*);

    Checkbox checkbox{};
    Text text{};
    SaveLoadFix::Settings settings;
    const char* error{};

    bool Edit(const char* label, const wchar_t* key, bool& setting, bool liveLogging = false)
    {
        bool selected = setting;
        if (!checkbox(label, &selected)) {
            return false;
        }
        if (liveLogging && !SaveLoadFix::SetDiagnosticLogging(selected)) {
            error = "Could not open SaveLoadFix.log.";
            return false;
        }
        if (!SaveLoadFix::Settings::Write(SaveLoadFix::Settings::path, key, selected)) {
            if (liveLogging) {
                SaveLoadFix::SetDiagnosticLogging(setting);
            }
            error = "Could not write SaveLoadFix.ini.";
            return false;
        }
        if (liveLogging && !selected) {
            SaveLoadFix::ResetMenuTrace();
        }
        setting = selected;
        error = nullptr;
        return true;
    }

    void __stdcall RenderSettings()
    {
        bool pruningChanged = Edit("Enable SKSE Cosave pruning", L"EnableCosavePruning", settings.enableCosavePruning);
        if (settings.enableCosavePruning) {
            pruningChanged |= Edit("Back up pruned SKSE Cosaves", L"BackupPrunedCosaves", settings.backupPrunedCosaves);
        }
        if (pruningChanged) {
            using enum SaveLoadFix::PruneMode;
            SaveLoadFix::RequestCosavePruning(settings.enableCosavePruning, settings.backupPrunedCosaves ? backup : remove);
        }
        if (Edit("Enable debug logging", L"EnableLogging", settings.enableLogging, true) && settings.enableLogging) {
            SaveLoadFix::LogDiagnosticContext(settings);
        }
        if (error) {
            text(error, nullptr);
        }
    }
}

void SaveLoadFix::RegisterSettingsMenu()
{
    const auto framework = GetModuleHandleW(L"SKSEMenuFramework.dll");
    if (!framework) {
        return;
    }
    const auto addSection = reinterpret_cast<AddSectionItem>(GetProcAddress(framework, "AddSectionItem"));
    checkbox = reinterpret_cast<Checkbox>(GetProcAddress(framework, "igCheckbox"));
    text = reinterpret_cast<Text>(GetProcAddress(framework, "igTextUnformatted"));
    if (addSection && checkbox && text) {
        settings = Settings::Load(Settings::path);
        addSection("Swift Save Menus/Settings", RenderSettings);
    }
}
