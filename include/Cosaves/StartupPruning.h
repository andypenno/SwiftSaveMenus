#pragma once

#include <Cosaves/Prune.h>

#include <chrono>
#include <string>

namespace SaveLoadFix
{
    std::wstring VerifiedDefaultSaveFolder(const char* search) noexcept;
    bool NeedsPruningLocation() noexcept;
    void SetPruningLocation(std::wstring profile) noexcept;
    void RequestCosavePruning(bool enabled, PruneMode mode) noexcept;
    void CancelCosavePruning() noexcept;
    void DeferCosavePruning(std::chrono::milliseconds delay = std::chrono::seconds(30)) noexcept;
}
