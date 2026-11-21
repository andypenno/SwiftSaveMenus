#pragma once

#include <spdlog/logger.h>

namespace SwiftSaveMenus
{
    struct Settings;

    bool SetDiagnosticLogging(bool enabled) noexcept;
    spdlog::logger* DiagnosticLogger() noexcept;
    void LogDiagnosticContext(const Settings& settings);
}
