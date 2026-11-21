#include <SKSE/SKSE.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include <Configuration/DiagnosticLogging.h>
#include <Cosaves/StartupPruning.h>
#include <Search/PortableProfile.h>

namespace
{
    constexpr unsigned modeMask = 3;
    constexpr unsigned removeRequest = 1;
    constexpr unsigned backupRequest = 2;
    constexpr unsigned deferredRequest = 3;
    constexpr unsigned configuredShift = 2;
    constexpr std::uint64_t afterSaveBit = 16;
    constexpr std::uint64_t modeBits = 31;
    constexpr std::uint64_t generationStep = modeBits + 1;
    std::atomic_uint64_t request{};
    std::atomic_bool workerActive{};
    std::atomic<std::shared_ptr<const std::wstring>> location;
    std::atomic_bool locationKnown{};
    std::atomic<PTP_TIMER> saveTimer{};
    std::atomic_uint64_t quietUntil{};

    bool IsRunnable(std::uint64_t ticket)
    {
        const auto action = ticket & modeMask;
        return action == removeRequest || action == backupRequest;
    }

    std::filesystem::path DefaultSaveFolder()
    {
        const auto skseLogs = SKSE::log::log_directory();
        return skseLogs ? skseLogs->parent_path() / L"Saves" : std::filesystem::path{};
    }

    bool IsCurrentProfile(const std::filesystem::path& saves)
    {
        if (GetModuleHandleW(L"usvfs_x64.dll")) {
            return SwiftSaveMenus::PortableProfile(L"__MO_Saves") == saves.native();
        }
        return DefaultSaveFolder() == saves;
    }

    void Sweep(std::uint64_t ticket)
    {
        const auto thread = GetCurrentThread();
        const bool background = SetThreadPriority(thread, THREAD_MODE_BACKGROUND_BEGIN) != FALSE;
        const auto profile = location.load();
        const auto mode = (ticket & modeMask) == backupRequest ? SwiftSaveMenus::PruneMode::backup : SwiftSaveMenus::PruneMode::remove;
        const auto cancelled = [ticket] { return request.load() != ticket; };
        try {
            const auto* action = mode == SwiftSaveMenus::PruneMode::backup ? "backup" : "delete";
            if (auto* logger = SwiftSaveMenus::DiagnosticLogger()) {
                logger->info("SKSE Cosave sweep started (mode={}, trigger={})",
                    action, ticket & afterSaveBit ? "save_quiet_period" : "startup_or_settings");
            }
            SwiftSaveMenus::PruneReport report;
            const auto count = SwiftSaveMenus::PruneStaleCosaves(*profile, mode, cancelled, IsCurrentProfile, &report);
            if (auto* logger = SwiftSaveMenus::DiagnosticLogger()) {
                logger->info("SKSE Cosave sweep finished (mode={}, status={}, cancelled={}, candidates={}, pruned={}, skipped={}, failed={})",
                    action, report.status, cancelled(), report.candidates, count, report.skipped, report.failed);
            }
        } catch (const std::exception&) {
            if (auto* logger = SwiftSaveMenus::DiagnosticLogger()) {
                logger->warn("SKSE Cosave sweep stopped after an exception");
            }
        }
        if (background) {
            SetThreadPriority(thread, THREAD_MODE_BACKGROUND_END);
        }
    }

    void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context) noexcept
    {
        FreeLibraryWhenCallbackReturns(instance, static_cast<HMODULE>(context));
        for (;;) {
            auto ticket = request.load();
            if (IsRunnable(ticket)) {
                Sweep(ticket);
                if (!request.compare_exchange_strong(ticket, ticket & ~std::uint64_t{modeMask})) {
                    continue;
                }
            }
            workerActive.store(false);
            if (!IsRunnable(request.load()) || workerActive.exchange(true)) {
                return;
            }
        }
    }

    void StartPendingWork() noexcept
    {
        if (!IsRunnable(request.load()) || !locationKnown.load() || workerActive.exchange(true)) {
            return;
        }
        HMODULE module{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Run), &module)) {
            if (TrySubmitThreadpoolCallback(Run, module, nullptr)) {
                return;
            }
            FreeLibrary(module);
        }
        workerActive.store(false);
    }

    void ArmSaveTimer(PTP_TIMER timer)
    {
        const auto now = GetTickCount64();
        const auto until = quietUntil.load();
        const auto waitMillis = until > now ? until - now : 1;
        const auto relative = 0ULL - waitMillis * 10'000;
        FILETIME due{ static_cast<DWORD>(relative), static_cast<DWORD>(relative >> 32) };
        SetThreadpoolTimer(timer, &due, 0, 0);
    }

    void CALLBACK AfterSave(PTP_CALLBACK_INSTANCE, void*, PTP_TIMER timer) noexcept
    {
        auto ticket = request.load();
        if ((ticket & modeMask) != deferredRequest) {
            return;
        }
        if (GetTickCount64() < quietUntil.load()) {
            ArmSaveTimer(timer);
            return;
        }
        const auto configured = (ticket >> configuredShift) & modeMask;
        if (request.compare_exchange_strong(ticket, (ticket & ~std::uint64_t{modeMask}) | configured | afterSaveBit)) {
            StartPendingWork();
        }
    }

    std::uint64_t NextGeneration(std::uint64_t previous)
    {
        return (previous & ~modeBits) + generationStep;
    }
}

std::wstring SwiftSaveMenus::VerifiedDefaultSaveFolder(const char* search) noexcept
{
    if (!search || GetModuleHandleW(L"usvfs_x64.dll")) {
        return {};
    }
    try {
        wchar_t wide[MAX_PATH];
        if (!MultiByteToWideChar(AreFileApisANSI() ? CP_ACP : CP_OEMCP, 0, search, -1, wide, MAX_PATH)) {
            return {};
        }
        const std::filesystem::path pattern(wide);
        const auto expected = DefaultSaveFolder();
        std::error_code error;
        if (!expected.empty() && pattern.parent_path().is_absolute() &&
            std::filesystem::equivalent(expected, pattern.parent_path(), error)) {
            return expected.native();
        }
    } catch (const std::exception&) {
        // The search can continue when the cleanup location cannot be verified.
    }
    return {};
}

bool SwiftSaveMenus::NeedsPruningLocation() noexcept
{
    return !locationKnown.load();
}

void SwiftSaveMenus::SetPruningLocation(std::wstring profile) noexcept
{
    if (profile.empty() || locationKnown.load()) {
        return;
    }
    try {
        std::shared_ptr<const std::wstring> expected;
        if (location.compare_exchange_strong(expected, std::make_shared<const std::wstring>(std::move(profile)))) {
            const auto timer = CreateThreadpoolTimer(AfterSave, nullptr, nullptr);
            saveTimer.store(timer);
            locationKnown.store(true);
            if (timer && (request.load() & modeMask) == deferredRequest) {
                ArmSaveTimer(timer);
            }
            StartPendingWork();
        }
    } catch (const std::bad_alloc&) {
        // A later verified search can retry publication.
    }
}

void SwiftSaveMenus::RequestCosavePruning(bool enabled, PruneMode mode) noexcept
{
    const auto selected = !enabled ? 0u : mode == PruneMode::backup ? backupRequest : removeRequest;
    auto previous = request.load();
    while (!request.compare_exchange_weak(previous, NextGeneration(previous) | (selected << configuredShift) | selected)) {
    }
    if (enabled) {
        StartPendingWork();
    }
}

void SwiftSaveMenus::CancelCosavePruning() noexcept
{
    auto previous = request.load();
    while (!request.compare_exchange_weak(previous, (previous & ~std::uint64_t{modeMask}) + generationStep)) {
    }
}

void SwiftSaveMenus::DeferCosavePruning(std::chrono::milliseconds delay) noexcept
{
    quietUntil.store(GetTickCount64() + static_cast<std::uint64_t>(delay.count()));
    auto previous = request.load();
    do {
        if (((previous >> configuredShift) & modeMask) == 0) {
            return;
        }
    } while (!request.compare_exchange_weak(previous,
        ((previous & ~std::uint64_t{modeMask}) + generationStep) | deferredRequest));
    if (const auto timer = saveTimer.load()) {
        ArmSaveTimer(timer);
    }
}
