#include <Configuration/DiagnosticLogging.h>
#include <Cosaves/StartupPruning.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    fs::path profile;

    bool CheckSaveDeferral(const std::wstring& verified)
    {
        using namespace SwiftSaveMenus;
        using enum PruneMode;
        const auto first = profile / "Saves" / "Orphan00000.skse";
        RequestCosavePruning(true, backup);
        DeferCosavePruning(300ms);
        SetPruningLocation(verified);
        std::this_thread::sleep_for(180ms);
        DeferCosavePruning(300ms);
        std::this_thread::sleep_for(180ms);
        if (!fs::exists(first)) {
            return false;
        }
        CancelCosavePruning();
        std::this_thread::sleep_for(350ms);
        if (!fs::exists(first)) {
            return false;
        }

        DeferCosavePruning(150ms);
        const auto limit = Clock::now() + 5s;
        const auto recovered = profile / "SwiftSaveMenus-cosave-recovery" / first.filename();
        while (!fs::exists(recovered) && Clock::now() < limit) {
            std::this_thread::sleep_for(2ms);
        }
        RequestCosavePruning(false, backup);
        return fs::exists(recovered) && !fs::exists(first);
    }
}

namespace SKSE::log
{
    std::optional<fs::path> log_directory()
    {
        return profile / "SKSE";
    }
}

spdlog::logger* SwiftSaveMenus::DiagnosticLogger() noexcept
{
    return nullptr;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        return 2;
    }
    profile = fs::absolute(argv[1]);
    const auto saves = profile / "Saves";
    const auto pattern = (saves / "*.ess").string();
    const auto verified = SwiftSaveMenus::VerifiedDefaultSaveFolder(pattern.c_str());
    const auto other = (profile / "Elsewhere" / "*.ess").string();
    if (verified != saves.native() || !SwiftSaveMenus::VerifiedDefaultSaveFolder(other.c_str()).empty()) {
        return 1;
    }
    using namespace SwiftSaveMenus;
    using enum PruneMode;
    const auto mode = std::string_view(argv[2]);
    if (mode == "after-save") {
        return CheckSaveDeferral(verified) ? 0 : 1;
    }
    if (mode != "exit") {
        SetPruningLocation(verified);
        CancelCosavePruning();
    }
    const auto started = Clock::now();
    RequestCosavePruning(true, remove);
    if (mode == "exit") {
        SetPruningLocation(verified);
    }
    std::cout << "Queued after " << (Clock::now() - started) / 1us << " us" << std::endl;
    if (mode != "empty") {
        const auto limit = started + 25s;
        while (fs::exists(saves / "Orphan00000.skse")) {
            if (Clock::now() >= limit) {
                std::cerr << "No orphan removed before timeout\n";
                return 1;
            }
            std::this_thread::sleep_for(2ms);
        }
        std::cout << "First orphan removed after " << (Clock::now() - started) / 1ms << " ms" << std::endl;
        if (mode == "exit") {
            return fs::exists(saves / "Orphan00127.skse") ? 0 : 1;
        }
    }
    const auto cancelling = Clock::now();
    RequestCosavePruning(false, remove);
    std::cout << "Cancelled after " << (Clock::now() - cancelling) / 1us << " us" << std::endl;
    if (mode == "cancel") {
        const auto last = saves / "Orphan00127.skse";
        if (!fs::exists(last)) {
            std::cerr << "Worker completed before cancellation\n";
            return 1;
        }
        std::this_thread::sleep_for(40ms);
        if (!fs::exists(last)) {
            return 1;
        }
        RequestCosavePruning(true, backup);
        const auto firstBackup = profile / "SwiftSaveMenus-cosave-recovery" / "Orphan00127.skse";
        const auto limit = Clock::now() + 25s;
        while (!fs::exists(firstBackup) && Clock::now() < limit) {
            std::this_thread::sleep_for(2ms);
        }
        CancelCosavePruning();
        return fs::exists(firstBackup) ? 0 : 1;
    }
    return 0;
}
