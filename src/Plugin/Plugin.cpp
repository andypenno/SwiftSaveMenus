#include <SKSE/SKSE.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Configuration/DiagnosticLogging.h>
#include <Configuration/Settings.h>
#include <Cosaves/StartupPruning.h>
#include <Menu/MenuTiming.h>
#include <Menu/SettingsMenu.h>
#include <Plugin/RuntimeSupport.h>
#include <Search/DirectoryHooks.h>

namespace
{
    SwiftSaveMenus::Settings settings;
    REL::Version skseVersion;

    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        if (message->type == SKSE::MessagingInterface::kPostPostLoad) {
            using enum SwiftSaveMenus::PruneMode;
            SwiftSaveMenus::RequestCosavePruning(settings.enableCosavePruning, settings.backupPrunedCosaves ? backup : remove);
            SwiftSaveMenus::InstallDirectoryHooks();
            SwiftSaveMenus::InstallMenuTiming();
        } else if (message->type == SKSE::MessagingInterface::kDataLoaded && settings.enableSettingsMenu) {
            SwiftSaveMenus::RegisterSettingsMenu();
        } else if (message->type == SKSE::MessagingInterface::kSaveGame) {
            SwiftSaveMenus::DeferCosavePruning();
        } else if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
                   message->type == SKSE::MessagingInterface::kNewGame) {
            SwiftSaveMenus::CancelCosavePruning();
        }
    }
}

void SwiftSaveMenus::LogDiagnosticContext(const Settings& current)
{
    if (auto* logger = DiagnosticLogger()) {
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        using WineVersion = const char* (*)();
        const auto wineVersion = reinterpret_cast<WineVersion>(GetProcAddress(ntdll, "wine_get_version"));
        logger->info("Swift Save Menus {}, Skyrim {}, SKSE {}, Wine={}, MO2={}, pruning={}, backup={}, settings menu={}",
            SKSE::GetPluginVersion().string(), REL::Module::get().version().string(), skseVersion.string(),
            wineVersion ? wineVersion() : "no", GetModuleHandleW(L"usvfs_x64.dll") != nullptr,
            current.enableCosavePruning, current.backupPrunedCosaves, current.enableSettingsMenu);
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    if (!skse || skse->IsEditor()) {
        return false;
    }
    if (!SwiftSaveMenus::SupportsRuntime(skse->RuntimeVersion().pack(), skse->SKSEVersion())) {
        return false;
    }
    settings = SwiftSaveMenus::Settings::Load(SwiftSaveMenus::Settings::path);
    skseVersion = REL::Version::unpack(skse->SKSEVersion());
    SKSE::Init(skse, false);
    SwiftSaveMenus::SetDiagnosticLogging(settings.enableLogging);
    SwiftSaveMenus::LogDiagnosticContext(settings);
    const auto* messaging = SKSE::GetMessagingInterface();
    return messaging && messaging->RegisterListener(OnMessage);
}
