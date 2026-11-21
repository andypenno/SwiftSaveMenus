#pragma once

#include <algorithm>
#include <string_view>

namespace SaveLoadFix
{
    inline bool IsSaveSearch(std::string_view path)
    {
        constexpr auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; };
        const auto matches = [&](std::string_view component, std::string_view name) {
            return std::ranges::equal(component, name, {}, lower);
        };
        while (!path.empty()) {
            const auto separator = path.find_first_of("\\/");
            const auto part = path.substr(0, separator);
            if (matches(part, "__mo_saves") || matches(part, "saves") || matches(part, "*.ess")) {
                return true;
            }
            if (separator == std::string_view::npos) {
                break;
            }
            path.remove_prefix(separator + 1);
        }
        return false;
    }
}
