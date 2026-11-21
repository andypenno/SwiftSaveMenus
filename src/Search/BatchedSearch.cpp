#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>

#include <Search/BatchedSearch.h>
#include <Search/PortableProfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace SaveLoadFix
{
    namespace
    {
        struct DirectoryEntry
        {
            ULONG nextOffset;
            ULONG fileIndex;
            LARGE_INTEGER creation, access, write, change, size, allocation;
            ULONG attributes, nameBytes, eaSize;
            UCHAR shortNameBytes;
            WCHAR shortName[12];
        };
        constexpr auto nameOffset = offsetof(DirectoryEntry, shortName) + sizeof(DirectoryEntry::shortName);
        static_assert(nameOffset == 94);

        struct Search
        {
            std::vector<WIN32_FIND_DATAA> entries;
            std::size_t next{ 1 };
        };

        using Query = NTSTATUS (NTAPI*)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK,
            PVOID, ULONG, FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING, BOOLEAN);
        constexpr auto bothDirectoryInfo = static_cast<FILE_INFORMATION_CLASS>(3);
        constexpr NTSTATUS noMoreFiles = static_cast<NTSTATUS>(0x80000006);
        constexpr NTSTATUS noSuchFile = static_cast<NTSTATUS>(0xC000000F);

        std::mutex searchMutex;
        std::unordered_map<HANDLE, Search> searches;
        std::atomic_size_t activeSearches{};

        bool ConvertName(const WCHAR* name, ULONG bytes, char* output, int capacity)
        {
            if (!bytes) {
                return true;
            }
            const auto codePage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
            const auto length = WideCharToMultiByte(codePage, 0, name, bytes / sizeof(WCHAR),
                output, capacity - 1, nullptr, nullptr);
            return length != 0;
        }

        bool AppendBatch(const std::byte* buffer, std::size_t length, Search& search)
        {
            std::size_t offset = 0;
            while (length - offset >= nameOffset) {
                DirectoryEntry entry{};
                std::memcpy(&entry, buffer + offset, nameOffset);
                const auto recordBytes = entry.nextOffset ? entry.nextOffset : length - offset;
                if (recordBytes < nameOffset || recordBytes > length - offset ||
                    entry.nameBytes == 0 || entry.nameBytes > recordBytes - nameOffset ||
                    entry.nameBytes % sizeof(WCHAR) || entry.shortNameBytes % sizeof(WCHAR) ||
                    entry.shortNameBytes > sizeof(entry.shortName) || entry.nameBytes >= MAX_PATH * sizeof(WCHAR)) {
                    return false;
                }
                WIN32_FIND_DATAA found{};
                found.dwFileAttributes = entry.attributes;
                found.ftCreationTime = { entry.creation.LowPart, static_cast<DWORD>(entry.creation.HighPart) };
                found.ftLastAccessTime = { entry.access.LowPart, static_cast<DWORD>(entry.access.HighPart) };
                found.ftLastWriteTime = { entry.write.LowPart, static_cast<DWORD>(entry.write.HighPart) };
                found.nFileSizeHigh = static_cast<DWORD>(entry.size.HighPart);
                found.nFileSizeLow = entry.size.LowPart;
                if (entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    found.dwReserved0 = entry.eaSize;
                }
                std::array<WCHAR, MAX_PATH> name;
                std::memcpy(name.data(), buffer + offset + nameOffset, entry.nameBytes);
                if (!ConvertName(name.data(), entry.nameBytes, found.cFileName, sizeof(found.cFileName)) ||
                    !ConvertName(entry.shortName, entry.shortNameBytes, found.cAlternateFileName, sizeof(found.cAlternateFileName))) {
                    return false;
                }
                search.entries.push_back(found);
                if (!entry.nextOffset) {
                    return true;
                }
                offset += entry.nextOffset;
            }
            return false;
        }

        bool ReadDirectory(Query query, HANDLE directory, Search& search)
        {
            // Keep the DOS wildcard comparison in Wine and the MO2 virtual filesystem.
            WCHAR pattern[] = L"*.ess";
            UNICODE_STRING mask{ sizeof(pattern) - sizeof(WCHAR), sizeof(pattern), pattern };
            constexpr ULONG capacity = 128 * 1024;
            const auto buffer = std::make_unique_for_overwrite<std::byte[]>(capacity);
            BOOLEAN restart = TRUE;
            while (true) {
                IO_STATUS_BLOCK status{};
                const auto result = query(directory, nullptr, nullptr, nullptr, &status, buffer.get(),
                    capacity, bothDirectoryInfo, FALSE, &mask, restart);
                if (result == noMoreFiles || result == noSuchFile) {
                    return true;
                }
                if (result != 0 || status.Information > capacity || !AppendBatch(buffer.get(), status.Information, search)) {
                    return false;
                }
                restart = FALSE;
            }
        }

        bool HasEmptyUnderlyingDirectory(HMODULE ntdll, Query query, std::wstring directory)
        {
            using Open = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
            const auto open = reinterpret_cast<Open>(GetProcAddress(ntdll, "NtOpenFile"));
            if (!open || directory.empty()) {
                return false;
            }
            const auto bytes = static_cast<USHORT>(directory.size() * sizeof(wchar_t));
            UNICODE_STRING name{ bytes, bytes, directory.data() };
            constexpr auto caseInsensitive = 0x40UL;
            OBJECT_ATTRIBUTES attributes{ sizeof(attributes), nullptr, &name, caseInsensitive, nullptr, nullptr };
            IO_STATUS_BLOCK status{};
            HANDLE handle{};
            constexpr auto directoryFile = 0x1UL;
            constexpr auto synchronousIo = 0x20UL;
            if (open(&handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &attributes, &status,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, directoryFile | synchronousIo) != 0) {
                return false;
            }
            WCHAR pattern[] = L"*.ess";
            UNICODE_STRING mask{ sizeof(pattern) - sizeof(WCHAR), sizeof(pattern), pattern };
            std::array<std::byte, 1024> buffer;
            const auto result = query(handle, nullptr, nullptr, nullptr, &status, buffer.data(),
                static_cast<ULONG>(buffer.size()), bothDirectoryInfo, TRUE, &mask, TRUE);
            CloseHandle(handle);
            return result == noMoreFiles || result == noSuchFile;
        }

    }

    void* FindFirstBatched(const char* path, void* data, bool* usedPhysicalProfile, std::wstring* verifiedProfile,
        const char** fallbackReason) noexcept
    {
        if (fallbackReason) {
            *fallbackReason = "unsupported_query";
        }
        if (!path || !data) {
            return nullptr;
        }
        const std::string_view searchPath{ path };
        const auto separator = searchPath.find_last_of("\\/");
        const auto maskStart = separator == std::string_view::npos ? 0 : separator + 1;
        if (maskStart >= MAX_PATH || _stricmp(path + maskStart, "*.ess") != 0) {
            return nullptr;
        }
        static const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        static const bool wine = GetProcAddress(ntdll, "wine_get_version") != nullptr;
        static const auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryDirectoryFile"));
        if (!query) {
            if (fallbackReason) {
                *fallbackReason = "directory_api_unavailable";
            }
            return nullptr;
        }
        char directory[MAX_PATH]{ '.' };
        if (maskStart != 0) {
            const bool driveRoot = separator == 0 || (separator == 2 && path[1] == ':');
            std::memcpy(directory, path, driveRoot ? maskStart : separator);
        }
        constexpr auto share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        HANDLE handle = INVALID_HANDLE_VALUE;
        try {
            if (fallbackReason) {
                *fallbackReason = "directory_open_failed";
            }
            Search search;
            WCHAR wideDirectory[MAX_PATH];
            std::wstring physical;
            if (wine && usedPhysicalProfile && MultiByteToWideChar(AreFileApisANSI() ? CP_ACP : CP_OEMCP,
                    0, directory, -1, wideDirectory, MAX_PATH)) {
                physical = PortableProfile(wideDirectory);
            }
            const bool usePhysical = !physical.empty() &&
                HasEmptyUnderlyingDirectory(ntdll, query, UnderlyingDirectory(wideDirectory));
            const auto retryVirtual = [&]() -> void* {
                if (!usePhysical) {
                    return nullptr;
                }
                *usedPhysicalProfile = false;
                return FindFirstBatched(path, data, nullptr, nullptr, fallbackReason);
            };
            handle = usePhysical
                ? CreateFileW(physical.c_str(), FILE_LIST_DIRECTORY, share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)
                : CreateFileA(directory, FILE_LIST_DIRECTORY, share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return retryVirtual();
            }
            if (!ReadDirectory(query, handle, search)) {
                if (fallbackReason) {
                    *fallbackReason = "directory_batch_failed";
                }
                CloseHandle(handle);
                return retryVirtual();
            }
            if (search.entries.empty()) {
                CloseHandle(handle);
                if (usePhysical) {
                    return retryVirtual();
                }
                if (fallbackReason) {
                    *fallbackReason = nullptr;
                }
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;
            }
            if (usePhysical) {
                if (!SameSave(physical, directory, search.entries.front().cFileName) ||
                    !SameSave(physical, directory, search.entries.back().cFileName)) {
                    CloseHandle(handle);
                    return retryVirtual();
                }
                for (auto& entry : search.entries) {
                    entry.cAlternateFileName[0] = '\0';
                }
                std::ranges::sort(search.entries, [](const auto& a, const auto& b) {
                    return _stricmp(a.cFileName, b.cFileName) < 0;
                });
            }
            const std::lock_guard lock(searchMutex);
            if (fallbackReason) {
                *fallbackReason = "search_handle_collision";
            }
            const auto [slot, inserted] = searches.try_emplace(handle, std::move(search));
            if (inserted) {
                ++activeSearches;
                if (fallbackReason) {
                    *fallbackReason = nullptr;
                }
                if (usedPhysicalProfile) {
                    *usedPhysicalProfile = usePhysical;
                }
                if (verifiedProfile && usePhysical) {
                    verifiedProfile->swap(physical);
                }
                std::memcpy(data, &slot->second.entries.front(), sizeof(WIN32_FIND_DATAA));
                return handle;
            }
        } catch (const std::bad_alloc&) {
            if (fallbackReason) {
                *fallbackReason = "allocation_failed";
            }
        }
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        return nullptr;
    }

    std::optional<int> FindNextBatched(void* handle, void* data)
    {
        if (activeSearches.load() == 0) {
            return std::nullopt;
        }
        const std::lock_guard lock(searchMutex);
        const auto slot = searches.find(handle);
        if (slot == searches.end()) {
            return std::nullopt;
        }
        auto& search = slot->second;
        if (search.next == search.entries.size()) {
            SetLastError(ERROR_NO_MORE_FILES);
            return FALSE;
        }
        if (!data) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        std::memcpy(data, &search.entries[search.next++], sizeof(WIN32_FIND_DATAA));
        return TRUE;
    }

    std::optional<int> FindCloseBatched(void* handle)
    {
        if (activeSearches.load() == 0) {
            return std::nullopt;
        }
        {
            const std::lock_guard lock(searchMutex);
            if (!searches.erase(handle)) {
                return std::nullopt;
            }
            --activeSearches;
        }
        return CloseHandle(handle);
    }
}
