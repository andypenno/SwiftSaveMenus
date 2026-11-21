#include <SKSE/SKSE.h>

#include <Configuration/DiagnosticLogging.h>
#include <Cosaves/StartupPruning.h>
#include <Search/BatchedSearch.h>
#include <Search/DirectoryHooks.h>
#include <Search/SavePath.h>

extern "C" __declspec(dllimport) void __stdcall SetLastError(unsigned long error);

namespace
{
    using Clock = std::chrono::steady_clock;
    using FindHandle = REX::W32::HANDLE;
    using FindData = REX::W32::WIN32_FIND_DATAA;
    FindHandle (*findFirst)(const char*, FindData*){};
    int (*findNext)(FindHandle, FindData*){};
    int (*findClose)(FindHandle){};

    struct DirectoryScan
    {
        FindHandle handle{};
        Clock::time_point start{};
        Clock::duration apiTime{};
        std::size_t entries{};
        std::size_t nextCalls{};
    };

    std::array<DirectoryScan, 32> scans{};
    std::mutex scansMutex;
    std::atomic_size_t activeScans{};

    FindHandle FindFirstTimed(const char* path, FindData* data)
    {
        auto* logger = SwiftSaveMenus::DiagnosticLogger();
        const auto start = logger ? Clock::now() : Clock::time_point{};
        const auto incomingError = REX::W32::GetLastError();
        bool physical = false;
        const char* fallbackReason{};
        std::wstring verifiedProfile;
        const bool needsLocation = SwiftSaveMenus::NeedsPruningLocation();
        if (const auto batched = SwiftSaveMenus::FindFirstBatched(path, data, &physical,
                needsLocation ? &verifiedProfile : nullptr, logger ? &fallbackReason : nullptr)) {
            const auto searchError = REX::W32::GetLastError();
            if (needsLocation && !physical && verifiedProfile.empty()) {
                verifiedProfile = SwiftSaveMenus::VerifiedDefaultSaveFolder(path);
            }
            if (needsLocation) {
                SwiftSaveMenus::SetPruningLocation(std::move(verifiedProfile));
                if (logger && SwiftSaveMenus::NeedsPruningLocation() &&
                    (physical || (path && SwiftSaveMenus::IsSaveSearch(path)))) {
                    logger->info("SKSE Cosave pruning location is still unverified");
                }
            }
            if (batched == REX::W32::INVALID_HANDLE_VALUE) {
                SetLastError(searchError);
                return batched;
            }
            if (logger) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
                logger->info("Batched save search ready after {} us (physical MO2 profile={})", elapsed.count(), physical);
            }
            SetLastError(incomingError);
            return batched;
        }
        SetLastError(incomingError);
        const auto handle = findFirst(path, data);
        if (!logger) {
            return handle;
        }
        const auto error = REX::W32::GetLastError();
        const auto elapsed = Clock::now() - start;
        if (path && SwiftSaveMenus::IsSaveSearch(path)) {
            const bool opened = handle != REX::W32::INVALID_HANDLE_VALUE;
            if (opened) {
                const std::lock_guard lock(scansMutex);
                const auto slot = std::ranges::find(scans, FindHandle{}, &DirectoryScan::handle);
                if (slot != scans.end()) {
                    *slot = { handle, start, elapsed, 1 };
                    ++activeScans;
                }
            }
            logger->info("Original save search took {} us (reason={}, opened={}, win32_error={})",
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(), fallbackReason, opened, opened ? 0 : error);
        }
        SetLastError(error);
        return handle;
    }

    int FindNextTimed(FindHandle handle, FindData* data)
    {
        if (const auto batched = SwiftSaveMenus::FindNextBatched(handle, data)) {
            return *batched;
        }
        if (!SwiftSaveMenus::DiagnosticLogger() || activeScans.load() == 0) {
            return findNext(handle, data);
        }
        const auto start = Clock::now();
        const auto result = findNext(handle, data);
        const auto error = REX::W32::GetLastError();
        const auto elapsed = Clock::now() - start;
        {
            const std::lock_guard lock(scansMutex);
            const auto slot = std::ranges::find(scans, handle, &DirectoryScan::handle);
            if (slot != scans.end()) {
                slot->apiTime += elapsed;
                ++slot->nextCalls;
                slot->entries += result != 0;
            }
        }
        SetLastError(error);
        return result;
    }

    int FindCloseTimed(FindHandle handle)
    {
        if (const auto batched = SwiftSaveMenus::FindCloseBatched(handle)) {
            return *batched;
        }
        if (activeScans.load() == 0) {
            return findClose(handle);
        }
        DirectoryScan scan;
        {
            const std::lock_guard lock(scansMutex);
            const auto slot = std::ranges::find(scans, handle, &DirectoryScan::handle);
            if (slot != scans.end()) {
                scan = std::exchange(*slot, {});
                --activeScans;
            }
        }
        const auto start = Clock::now();
        const auto result = findClose(handle);
        const auto error = REX::W32::GetLastError();
        const auto finish = Clock::now();
        if (auto* logger = SwiftSaveMenus::DiagnosticLogger(); logger && scan.handle) {
            using Microseconds = std::chrono::microseconds;
            logger->info("Save directory scan: {} entries, {} next calls, {} us in APIs, {} us elapsed",
                scan.entries, scan.nextCalls,
                std::chrono::duration_cast<Microseconds>(scan.apiTime + finish - start).count(),
                std::chrono::duration_cast<Microseconds>(finish - scan.start).count());
        }
        SetLastError(error);
        return result;
    }

    template <class Function>
    bool InstallImport(std::string_view name, Function replacement, Function& original)
    {
        auto* slot = SKSE::GetIATPtr<Function>("KERNEL32.dll", name);
        if (!slot || !*slot) {
            return false;
        }
        original = *slot;
        const auto expected = original;
        return REL::safe_write(reinterpret_cast<std::uintptr_t>(slot), &replacement,
            sizeof(replacement), &expected, sizeof(expected)) && *slot == replacement;
    }

}

void SwiftSaveMenus::InstallDirectoryHooks()
{
    const auto close = InstallImport("FindClose", FindCloseTimed, findClose);
    const auto next = close && InstallImport("FindNextFileA", FindNextTimed, findNext);
    const auto first = next && InstallImport("FindFirstFileA", FindFirstTimed, findFirst);
    if (auto* logger = DiagnosticLogger()) {
        logger->info("Directory hooks installed: first={}, next={}, close={}", first, next, close);
    }
}
