#pragma once

#include <string>
#include <string_view>

namespace SaveLoadFix
{
    std::wstring PortableProfile(std::wstring_view virtualDirectory);
    bool SameSave(std::wstring_view physical, const char* virtualDirectory, const char* filename);
    std::wstring UnderlyingDirectory(std::wstring_view virtualDirectory);
}
