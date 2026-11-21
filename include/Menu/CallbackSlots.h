#pragma once

#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace SwiftSaveMenus
{
    inline bool IsSaveCallback(std::string_view name)
    {
        return name == "SAVE" || name == "LOAD" || name.contains("Save") ||
               name.contains("Load") || name.contains("Character");
    }

    template <class Callback, std::size_t Capacity>
    class CallbackSlots
    {
    public:
        Callback Select(std::string_view name, Callback original, const std::array<Callback, Capacity>& probes)
        {
            if (!original || !IsSaveCallback(name)) {
                return original;
            }

            const std::lock_guard lock(mutex_);
            for (std::size_t i = 0; i < used_; ++i) {
                if (entries_[i].original == original && entries_[i].name == name) {
                    return probes[i];
                }
            }
            if (used_ == Capacity) {
                return original;
            }
            entries_[used_] = { std::string(name), original };
            return probes[used_++];
        }

        std::pair<std::string_view, Callback> Get(std::size_t index)
        {
            const std::lock_guard lock(mutex_);
            const auto& entry = entries_[index];
            return { entry.name, entry.original };
        }

    private:
        struct Entry
        {
            std::string name;
            Callback original{};
        };

        std::mutex mutex_;
        std::array<Entry, Capacity> entries_{};
        std::size_t used_{};
    };
}
