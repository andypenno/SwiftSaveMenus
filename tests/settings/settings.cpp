#include <Configuration/Settings.h>

#include <fstream>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        return 2;
    }
    const std::filesystem::path path = argv[1];
    const auto defaults = SwiftSaveMenus::Settings::Load(path);
    if (std::filesystem::exists(path) || defaults.enableLogging || !defaults.enableCosavePruning ||
        defaults.backupPrunedCosaves || !defaults.enableSettingsMenu) {
        return 1;
    }
    for (const auto* value : { "0", "1", "invalid" }) {
        std::ofstream(path) << "[General]\nEnableLogging=" << value << "\nEnableCosavePruning=" << value
                            << "\nBackupPrunedCosaves=" << value << "\nEnableSettingsMenu=" << value << '\n';
        const auto settings = SwiftSaveMenus::Settings::Load(path);
        const bool enabled = std::string_view(value) == "1";
        if (settings.enableLogging != enabled || settings.enableCosavePruning != enabled ||
            settings.backupPrunedCosaves != enabled || settings.enableSettingsMenu != enabled) {
            std::cerr << "Unexpected setting for " << value << '\n';
            return 1;
        }
    }
    std::ofstream(path) << "[General]\nEnableLogging=0\nEnableCosavePruning=0\nEnableSettingsMenu=0\n[Other]\nPreserve=sentinel\n";
    using SwiftSaveMenus::Settings;
    if (!Settings::Write(path, L"BackupPrunedCosaves", true) || !Settings::Load(path).backupPrunedCosaves ||
        !Settings::Write(path, L"EnableLogging", true) || !Settings::Load(path).enableLogging ||
        Settings::Load(path).enableCosavePruning || Settings::Load(path).enableSettingsMenu ||
        Settings::Write(path.parent_path() / "missing-directory" / "settings.ini", L"EnableLogging", true)) {
        return 1;
    }
    std::ifstream contents(path);
    const std::string saved{ std::istreambuf_iterator<char>(contents), {} };
    return saved.contains("Preserve=sentinel") ? 0 : 1;
}
