#pragma once

#include <array>
#include <cstdint>

namespace SwiftSaveMenus
{
    constexpr std::uint32_t PackVersion(unsigned major, unsigned minor, unsigned patch)
    {
        return major << 24 | minor << 16 | patch << 4;
    }

    struct RuntimeSupport
    {
        std::uint32_t game;
        std::uint32_t skse;
    };

    inline constexpr std::array supportedRuntimes{
        RuntimeSupport{ PackVersion(1, 4, 15), PackVersion(2, 0, 12) },
        RuntimeSupport{ PackVersion(1, 5, 97), PackVersion(2, 0, 20) },
        RuntimeSupport{ PackVersion(1, 6, 317), PackVersion(2, 1, 0) },
        RuntimeSupport{ PackVersion(1, 6, 318), PackVersion(2, 1, 1) },
        RuntimeSupport{ PackVersion(1, 6, 323), PackVersion(2, 1, 3) },
        RuntimeSupport{ PackVersion(1, 6, 342), PackVersion(2, 1, 4) },
        RuntimeSupport{ PackVersion(1, 6, 353), PackVersion(2, 1, 5) },
        RuntimeSupport{ PackVersion(1, 6, 629), PackVersion(2, 2, 0) },
        RuntimeSupport{ PackVersion(1, 6, 640), PackVersion(2, 2, 2) },
        RuntimeSupport{ PackVersion(1, 6, 659), PackVersion(2, 2, 2) },
        RuntimeSupport{ PackVersion(1, 6, 1130), PackVersion(2, 2, 4) },
        RuntimeSupport{ PackVersion(1, 6, 1170), PackVersion(2, 2, 6) },
        RuntimeSupport{ PackVersion(1, 6, 1179), PackVersion(2, 2, 6) },
        RuntimeSupport{ PackVersion(1, 7, 99), PackVersion(2, 3, 0) },
        RuntimeSupport{ PackVersion(1, 7, 104), PackVersion(2, 3, 1) },
    };

    constexpr bool SupportsRuntime(std::uint32_t game, std::uint32_t skse)
    {
        for (const auto& runtime : supportedRuntimes) {
            if (game == runtime.game) {
                return (skse >> 24) == (runtime.skse >> 24) && skse >= runtime.skse;
            }
        }
        return false;
    }
}
