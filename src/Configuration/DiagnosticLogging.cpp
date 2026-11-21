#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <Configuration/DiagnosticLogging.h>

namespace
{
    std::atomic<spdlog::logger*> activeLogger{};

    std::shared_ptr<spdlog::logger> OpenLog()
    {
        const auto directory = SKSE::log::log_directory();
        if (!directory) {
            throw std::runtime_error("SKSE log directory is unavailable");
        }
        const auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            (*directory / "SwiftSaveMenus.log").string(), true);
        auto logger = std::make_shared<spdlog::logger>("SwiftSaveMenus", sink);
        logger->set_pattern("[%T.%e] [%=5t] [%L] %v");
        logger->flush_on(spdlog::level::info);
        return logger;
    }
}

bool SwiftSaveMenus::SetDiagnosticLogging(bool enabled) noexcept
{
    if (!enabled) {
        activeLogger.store(nullptr);
        return true;
    }
    try {
        static const auto logger = OpenLog();
        activeLogger.store(logger.get());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

spdlog::logger* SwiftSaveMenus::DiagnosticLogger() noexcept
{
    return activeLogger.load();
}
