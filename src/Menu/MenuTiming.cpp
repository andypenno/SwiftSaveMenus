#include <SKSE/Impl/PCH.h>
#include <RE/F/FxDelegateArgs.h>
#include <RE/F/FxDelegateHandler.h>
#include <RE/F/FxResponseArgsBase.h>
#include <RE/G/GFxValue.h>
#include <RE/G/GString.h>
#include <RE/Offsets_VTABLE.h>
#include <SKSE/SKSE.h>

#include <Configuration/DiagnosticLogging.h>
#include <Menu/CallbackSlots.h>
#include <Menu/MenuTiming.h>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Processor = RE::FxDelegateHandler::CallbackProcessor;
    using Callback = RE::FxDelegateHandler::CallbackFn*;
    using Accept = void (*)(void*, Processor*);
    using InvokeBatch = void (*)(RE::GFxMovieView*, const char*, RE::FxResponseArgsBase&);

    constexpr std::size_t slotCount = 48;
    SwiftSaveMenus::CallbackSlots<Callback, slotCount> slots;
    Accept mainMenuAccept{};
    Accept systemTabAccept{};
    InvokeBatch invokeBatch{};
    unsigned trace{};
    std::optional<Clock::time_point> started;
    RE::GFxMovieView* tracedMovie{};
    bool screenshotRequested{};
    std::mutex traceMutex;

    void StartMenuTrace(std::string_view action, RE::GFxMovieView* movie)
    {
        const std::lock_guard lock(traceMutex);
        if (auto* logger = SwiftSaveMenus::DiagnosticLogger()) {
            started = Clock::now();
            tracedMovie = movie;
            screenshotRequested = false;
            logger->info("Menu trace {} started: {}", ++trace, action);
        }
    }

    void RecordFirstScreenshot(RE::GFxMovieView* movie)
    {
        const std::lock_guard lock(traceMutex);
        if (auto* logger = SwiftSaveMenus::DiagnosticLogger(); started && logger && movie == tracedMovie && !screenshotRequested) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - *started);
            logger->info("Menu trace {} first screenshot requested after {} us", trace, elapsed.count());
            screenshotRequested = true;
        }
    }

    void TimeBatch(RE::GFxMovieView* movie, const char* name, RE::FxResponseArgsBase& args)
    {
        invokeBatch(movie, name, args);
        auto* logger = SwiftSaveMenus::DiagnosticLogger();
        if (!logger) {
            return;
        }
        const auto completed = Clock::now();
        const std::lock_guard lock(traceMutex);
        if (!started || movie != tracedMovie) {
            return;
        }
        RE::GFxValue* values{};
        if (args.GetValues(&values) == 4 && values && values[1].IsBool() && values[2].IsNumber() && values[3].IsNumber()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(completed - *started);
            logger->info("Menu trace {} batch callback returned after {} us (initial={}, arg2={}, arg3={})",
                trace, elapsed.count(), values[1].GetBool(), values[2].GetNumber(), values[3].GetNumber());
        }
    }

    template <std::size_t Index>
    void TimeCallback(const RE::FxDelegateArgs& args)
    {
        const auto [name, original] = slots.Get(Index);
        auto* logger = SwiftSaveMenus::DiagnosticLogger();
        if (!logger) {
            original(args);
            return;
        }
        if (name == "SAVE" || name == "LOAD" || name == "CharacterSelected") {
            StartMenuTrace(name, args.GetMovie());
        } else if (name == "PrepSaveGameScreenshot") {
            RecordFirstScreenshot(args.GetMovie());
        }
        const auto start = Clock::now();
        original(args);
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
        if (SwiftSaveMenus::DiagnosticLogger()) {
            logger->info("Callback {} took {} us", name, duration.count());
        }
    }

    template <std::size_t... Indices>
    constexpr auto MakeProbes(std::index_sequence<Indices...>)
    {
        return std::array<Callback, sizeof...(Indices)>{ &TimeCallback<Indices>... };
    }

    class ProbeProcessor final : public Processor
    {
    public:
        explicit ProbeProcessor(Processor& destination) : destination_(destination) {}

        void Process(const RE::GString& name, Callback original) override
        {
            static constexpr auto probes = MakeProbes(std::make_index_sequence<slotCount>{});
            const std::string_view label{ name.data(), name.size() };
            const auto replacement = slots.Select(label, original, probes);
            destination_.Process(name, replacement);
        }

    private:
        Processor& destination_;
    };

    template <Accept& Original>
    void AcceptCallbacks(void* menu, Processor* processor)
    {
        if (!processor) {
            Original(menu, processor);
            return;
        }
        ProbeProcessor probe{ *processor };
        Original(menu, &probe);
    }

    bool IsInSegment(REL::Segment segment, std::uintptr_t address, std::size_t length)
    {
        return address >= segment.address() && length <= segment.size() &&
               address - segment.address() <= segment.size() - length;
    }

    bool InstallAccept(REL::VariantID id, std::uintptr_t expectedRva, Accept replacement, Accept& original)
    {
        const auto& module = REL::Module::get();
        const auto slot = id.address() + sizeof(Accept);
        if (!IsInSegment(module.segment(REL::Segment::rdata), slot, sizeof(Accept))) {
            return false;
        }
        original = reinterpret_cast<Accept>(module.base() + expectedRva);
        if (!IsInSegment(module.segment(REL::Segment::textx), reinterpret_cast<std::uintptr_t>(original), 1)) {
            return false;
        }
        const auto expected = original;
        return REL::safe_write(slot, &replacement, sizeof(replacement), &expected, sizeof(expected));
    }

    bool InstallBatchTiming()
    {
        const auto& module = REL::Module::get();
        const auto code = module.segment(REL::Segment::textx);
        const auto call = module.base() + 0x97A476;
        const auto target = module.base() + 0xFBE900;
        constexpr std::array<std::uint8_t, 5> expected{ 0xE8, 0x85, 0x44, 0x64, 0x00 };
        if (!IsInSegment(code, call, expected.size()) || !IsInSegment(code, target, 1) ||
            std::memcmp(reinterpret_cast<const void*>(call), expected.data(), expected.size()) != 0) {
            return false;
        }
        invokeBatch = reinterpret_cast<InvokeBatch>(target);
        SKSE::AllocTrampoline(32);
        return SKSE::GetTrampoline().write_call<5>(call, TimeBatch) == target;
    }

}

void SwiftSaveMenus::InstallMenuTiming()
{
    if (REL::Module::get().version() != REL::Version{ 1, 6, 1170, 0 }) {
        return;
    }
    const auto main = InstallAccept(RE::VTABLE_MainMenu[0], 0x941CC0,
        AcceptCallbacks<mainMenuAccept>, mainMenuAccept);
    const auto journal = InstallAccept(RE::VTABLE_Journal_SystemTab[0], 0x98E800,
        AcceptCallbacks<systemTabAccept>, systemTabAccept);
    const auto batch = InstallBatchTiming();
    if (auto* logger = DiagnosticLogger()) {
        logger->info("Callback probes installed: main menu={}, journal system tab={}, batch={}", main, journal, batch);
    }
}

void SwiftSaveMenus::ResetMenuTrace()
{
    const std::lock_guard lock(traceMutex);
    started.reset();
}
