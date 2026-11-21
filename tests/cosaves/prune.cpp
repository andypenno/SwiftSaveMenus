#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Cosaves/Prune.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2 && (argc != 3 || std::string_view(argv[2]) != "deck-fixtures")) {
        return 2;
    }
    using enum SwiftSaveMenus::PruneMode;
    using SwiftSaveMenus::PruneStaleCosaves;
    const auto saves = fs::absolute(argv[1]) / "saves";
    if (argc == 3) {
        Require(PruneStaleCosaves(saves, backup) == 1, "Expected only the Deck fixture orphan to move");
        Require(PruneStaleCosaves(saves, backup) == 0, "Deck fixture cleanup was not idempotent");
        return 0;
    }
    const auto recovery = saves.parent_path() / "SwiftSaveMenus-cosave-recovery";
    fs::create_directory(saves);
    fs::create_directory(recovery);
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
    for (const auto* name : { "Manual.SKSE", "AutoSave7.skse", "QuickSave1.skse", "Pending.skse", "orphan.skse", "collision.skse" }) {
        std::ofstream(saves / name) << name;
        fs::last_write_time(saves / name, old);
    }
    for (const auto* name : { "manual.ess", "AUTOSAVE7.ESS", "quicksave1.ess", "Pending.ess.tmp", "Recent.skse" }) {
        std::ofstream(saves / name) << name;
    }
    std::ofstream(recovery / "collision.skse") << "earlier recovery";
    const auto cancelled = [] { return true; };
    Require(PruneStaleCosaves(saves, backup, cancelled) == 0 && fs::exists(saves / "orphan.skse"),
        "Cancelled cleanup moved a file");
    Require(PruneStaleCosaves(saves, backup, {}, [](const fs::path&) { return false; }) == 0 &&
        fs::exists(saves / "orphan.skse"), "Cleanup moved a file after profile validation failed");
    SwiftSaveMenus::PruneReport report;
    Require(PruneStaleCosaves(saves, backup, {}, nullptr, &report) == 1, "Expected exactly one eligible cosave to move");
    Require(std::string_view(report.status) == "complete" && report.candidates == 2 &&
        report.failed == 1 && report.skipped == 0, "Incorrect sweep counters for the backup collision");
    Require(!fs::exists(saves / "orphan.skse") && fs::exists(recovery / "orphan.skse"), "Orphan was not recoverable");
    for (const auto* name : { "Manual.SKSE", "AutoSave7.skse", "QuickSave1.skse", "Pending.skse", "Recent.skse", "collision.skse" }) {
        Require(fs::exists(saves / name), "Protected cosave was moved");
    }
    std::string recovered;
    std::getline(std::ifstream(recovery / "collision.skse"), recovered);
    Require(recovered == "earlier recovery", "Existing recovery file was overwritten");
    Require(PruneStaleCosaves(saves, backup) == 0, "Repeated cleanup moved another file");

    for (int index = 0; index < 64; ++index) {
        auto file = saves / ("stale" + std::to_string(index) + ".skse");
        std::ofstream(file) << index;
        fs::last_write_time(file, old);
    }
    const auto stopAfterFirstBatch = [](const fs::path&) {
        static int visits = 0;
        return visits++ == 0;
    };
    const auto moved = PruneStaleCosaves(saves, backup, {}, stopAfterFirstBatch);
    Require(moved > 0 && moved <= 32, "Cleanup did not stop at the next profile check");
    Require(PruneStaleCosaves(saves, backup) == 64 - moved, "Interrupted cleanup did not resume");
    Require(fs::exists(saves / "AutoSave7.skse"), "Resumed cleanup moved a paired cosave");

    for (const auto* name : { "late-save.skse", "late-temp.skse", "unpaired.skse" }) {
        std::ofstream(saves / name) << name;
        fs::last_write_time(saves / name, old);
    }
    const auto addPartners = [](const fs::path& profile) {
        std::ofstream(profile / "LATE-SAVE.ESS") << "save appeared after inventory";
        std::ofstream(profile / "LATE-TEMP.SKSE.TMP") << "cosave write in progress";
        return true;
    };
    Require(PruneStaleCosaves(saves, backup, {}, addPartners) == 1,
        "Cleanup did not distinguish late partners from a confirmed orphan");
    Require(fs::exists(saves / "late-save.skse") && fs::exists(saves / "late-temp.skse") &&
        fs::exists(recovery / "unpaired.skse"), "Cleanup lost a cosave with a late partner");

    const auto interrupted = saves / "interrupted.skse";
    std::ofstream(interrupted) << "cancel after inventory";
    fs::last_write_time(interrupted, old);
    static bool stop{};
    Require(PruneStaleCosaves(saves, backup, [] { return stop; }, [](const fs::path&) {
        stop = true;
        return true;
    }) == 0 && fs::exists(interrupted), "Cancelled worker moved a candidate after inventory");

    const auto deleteProfile = saves.parent_path() / "delete-profile";
    const auto deleteSaves = deleteProfile / "Saves";
    fs::create_directories(deleteSaves);
    for (const auto* name : { "orphan.skse", "AutoSave7.SKSE", "late-save.skse", "late-temp.skse" }) {
        const auto file = deleteSaves / name;
        std::ofstream(file) << name;
        fs::last_write_time(file, old);
    }
    std::ofstream(deleteSaves / "autosave7.ess") << "paired save";
    Require(PruneStaleCosaves(deleteSaves, remove, {}, addPartners) == 1, "Expected only one permanent deletion");
    Require(!fs::exists(deleteSaves / "orphan.skse") &&
        !fs::exists(deleteProfile / "SwiftSaveMenus-cosave-recovery") &&
        fs::exists(deleteSaves / "AutoSave7.SKSE") && fs::exists(deleteSaves / "late-save.skse") &&
        fs::exists(deleteSaves / "late-temp.skse"), "Deletion changed a protected cosave or made a backup");
    Require(PruneStaleCosaves(deleteSaves, remove) == 0, "Permanent deletion was not idempotent");

    const auto inUse = deleteSaves / "in-use.skse";
    std::ofstream(inUse) << "open cosave";
    fs::last_write_time(inUse, old);
    const auto writer = CreateFileW(inUse.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(writer != INVALID_HANDLE_VALUE, "Could not open the cosave for writing");
    Require(PruneStaleCosaves(deleteSaves, remove, {}, nullptr, &report) == 0 && fs::exists(inUse), "Cleanup removed an open cosave");
    Require(report.candidates == 1 && report.skipped == 1 && report.failed == 0, "Incorrect sweep counters for an open cosave");
    Require(CloseHandle(writer), "Could not close the cosave writer");
    Require(PruneStaleCosaves(deleteSaves, remove) == 1 && !fs::exists(inUse), "Closed orphan remained after cleanup");
}
