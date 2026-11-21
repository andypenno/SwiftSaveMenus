#pragma once

#include <spdlog/logger.h>

namespace SaveLoadFix
{
    struct Settings;

    bool SetDiagnosticLogging(bool enabled) noexcept;
    spdlog::logger* DiagnosticLogger() noexcept;
    void LogDiagnosticContext(const Settings& settings);
}
