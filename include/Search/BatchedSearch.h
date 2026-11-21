#pragma once

#include <optional>
#include <string>

namespace SaveLoadFix
{
    // Null leaves the search to the caller. An invalid handle reports a completed empty search.
    void* FindFirstBatched(const char* path, void* data, bool* usedPhysicalProfile = nullptr,
        std::wstring* verifiedProfile = nullptr, const char** fallbackReason = nullptr) noexcept;
    std::optional<int> FindNextBatched(void* handle, void* data);
    std::optional<int> FindCloseBatched(void* handle);
}
