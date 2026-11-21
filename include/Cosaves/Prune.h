#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>

namespace SaveLoadFix
{
    enum class PruneMode { remove, backup };

    struct PruneReport
    {
        const char* status{ "complete" };
        std::size_t candidates{};
        std::size_t skipped{};
        std::size_t failed{};
    };

    std::size_t PruneStaleCosaves(const std::filesystem::path& saves, PruneMode mode,
        const std::function<bool()>& cancelled = {},
        bool (*isCurrentProfile)(const std::filesystem::path&) = nullptr, PruneReport* report = nullptr);
}
