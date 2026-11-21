#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Cosaves/Prune.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using FileHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

    struct FoldedLess
    {
        bool operator()(const std::wstring& a, const std::wstring& b) const
        {
            return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        }
    };

    struct Candidate
    {
        fs::path name;
        std::uint64_t modified;
        std::uint64_t size;

        bool operator==(const Candidate&) const = default;
    };

    bool IsCancelled(const std::function<bool()>& cancelled)
    {
        return cancelled && cancelled();
    }

    std::uint64_t WideValue(DWORD high, DWORD low)
    {
        return (static_cast<std::uint64_t>(high) << 32) | low;
    }

    std::uint64_t WriteTime(FILETIME time)
    {
        return WideValue(time.dwHighDateTime, time.dwLowDateTime);
    }

    using Candidates = std::map<std::wstring, Candidate, FoldedLess>;

    std::optional<Candidates> Inventory(const fs::path& folder, std::uint64_t cutoff, const std::function<bool()>& cancelled)
    {
        std::set<std::wstring, FoldedLess> protectedNames;
        Candidates candidates;
        WIN32_FIND_DATAW entry{};
        const auto handle = FindFirstFileExW((folder / L"*").c_str(), FindExInfoBasic, &entry,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE) {
            return std::nullopt;
        }
        const std::unique_ptr<void, decltype(&FindClose)> search(handle, &FindClose);
        do {
            if (IsCancelled(cancelled)) {
                return std::nullopt;
            }
            const fs::path name = entry.cFileName;
            const auto suffix = name.extension().wstring();
            const auto stem = name.stem();
            if (_wcsicmp(suffix.c_str(), L".ess") == 0 || _wcsicmp(suffix.c_str(), L".tmp") == 0) {
                protectedNames.insert(stem.wstring());
                if (_wcsicmp(suffix.c_str(), L".tmp") == 0) {
                    protectedNames.insert(stem.stem().wstring());
                }
            } else if (_wcsicmp(suffix.c_str(), L".skse") == 0) {
                const auto key = stem.wstring();
                if (protectedNames.contains(key) || candidates.contains(key)) {
                    protectedNames.insert(key);
                    continue;
                }
                if (entry.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) {
                    protectedNames.insert(key);
                    continue;
                }
                const auto modified = WriteTime(entry.ftLastWriteTime);
                if (modified <= cutoff) {
                    candidates.emplace(key, Candidate{ name, modified, WideValue(entry.nFileSizeHigh, entry.nFileSizeLow) });
                } else {
                    protectedNames.insert(key);
                }
            }
        } while (FindNextFileW(handle, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            return std::nullopt;
        }
        for (const auto& name : protectedNames) {
            candidates.erase(name);
        }
        return candidates;
    }

    bool IsAbsent(const fs::path& path)
    {
        return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    FileHandle OpenOrdinaryDirectory(const fs::path& path)
    {
        const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return { nullptr, &CloseHandle };
        }
        FileHandle directory(handle, &CloseHandle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return { nullptr, &CloseHandle };
        }
        FILE_CASE_SENSITIVE_INFO caseInfo{};
        if (GetFileInformationByHandleEx(handle, FileCaseSensitiveInfo, &caseInfo, sizeof(caseInfo)) &&
            (caseInfo.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR)) {
            return { nullptr, &CloseHandle };
        }
        return directory;
    }

    bool UpdateCandidate(HANDLE file, const fs::path& destination)
    {
        if (destination.empty()) {
            FILE_DISPOSITION_INFO remove{ TRUE };
            return SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove));
        }

        const auto nameBytes = destination.native().size() * sizeof(wchar_t);
        const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t);
        std::vector<std::byte> buffer(bytes);
        auto& rename = *reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename.ReplaceIfExists = FALSE;
        rename.FileNameLength = static_cast<DWORD>(nameBytes);
        std::memcpy(buffer.data() + offsetof(FILE_RENAME_INFO, FileName), destination.c_str(), nameBytes);
        return SetFileInformationByHandle(file, FileRenameInfo, &rename, static_cast<DWORD>(bytes));
    }
}

std::size_t SaveLoadFix::PruneStaleCosaves(const fs::path& saves, PruneMode mode, const std::function<bool()>& cancelled,
    bool (*isCurrentProfile)(const fs::path&), PruneReport* report)
{
    PruneReport unused;
    auto& result = report ? *report : unused;
    result = {};
    if (IsCancelled(cancelled)) {
        result.status = "cancelled";
        return 0;
    }
    if (!saves.is_absolute() || _wcsicmp(saves.filename().c_str(), L"saves") != 0) {
        result.status = "unverified_save_folder";
        return 0;
    }
    const auto saveDirectory = OpenOrdinaryDirectory(saves);
    if (!saveDirectory) {
        result.status = "save_folder_unavailable_or_unsafe";
        return 0;
    }

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    using FileTimeDuration = std::chrono::duration<std::uint64_t, std::ratio<1, 10'000'000>>;
    const auto cutoff = WriteTime(now) - FileTimeDuration(std::chrono::hours(24)).count();
    const auto planned = Inventory(saves, cutoff, cancelled);
    if (!planned) {
        result.status = IsCancelled(cancelled) ? "cancelled" : "inventory_failed";
        return 0;
    }
    result.candidates = planned->size();
    if (planned->empty()) {
        return 0;
    }
    fs::path root;
    FileHandle recoveryDirectory(nullptr, &CloseHandle);
    if (mode == PruneMode::backup) {
        root = saves.parent_path() / L"SaveLoadFix-cosave-recovery";
        fs::create_directory(root);
        recoveryDirectory = OpenOrdinaryDirectory(root);
        if (!recoveryDirectory) {
            result.status = "recovery_folder_unavailable_or_unsafe";
            return 0;
        }
    }
    const auto confirmed = Inventory(saves, cutoff, cancelled);
    if (!confirmed) {
        result.status = IsCancelled(cancelled) ? "cancelled" : "confirmation_inventory_failed";
        return 0;
    }
    std::size_t pruned = 0;
    std::size_t attempted = 0;
    for (const auto& [stem, candidate] : *planned) {
        if (IsCancelled(cancelled)) {
            result.status = "cancelled";
            break;
        }
        const auto current = confirmed->find(stem);
        if (current == confirmed->end() || current->second != candidate) {
            ++result.skipped;
            continue;
        }
        constexpr std::size_t batchSize = 32;
        if (isCurrentProfile && attempted++ % batchSize == 0) {
            if (attempted > 1) {
                Sleep(10);
            }
            if (IsCancelled(cancelled) || !isCurrentProfile(saves)) {
                result.status = IsCancelled(cancelled) ? "cancelled" : "profile_changed";
                break;
            }
        }
        const auto from = saves / candidate.name;
        const auto partner = saves / (stem + L".ess");
        const auto handle = CreateFileW(from.c_str(), FILE_READ_ATTRIBUTES | DELETE, 0,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            ++result.skipped;
            continue;
        }
        const FileHandle file(handle, &CloseHandle);
        BY_HANDLE_FILE_INFORMATION data{};
        if (GetFileInformationByHandle(handle, &data) &&
            !(data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
            WriteTime(data.ftLastWriteTime) == candidate.modified &&
            WideValue(data.nFileSizeHigh, data.nFileSizeLow) == candidate.size &&
            IsAbsent(saves / (stem + L".tmp")) && IsAbsent(saves / (stem + L".ess.tmp")) &&
            IsAbsent(saves / (stem + L".skse.tmp")) && IsAbsent(partner) && !IsCancelled(cancelled)) {
            if (UpdateCandidate(handle, root.empty() ? fs::path{} : root / candidate.name)) {
                ++pruned;
            } else {
                ++result.failed;
            }
        } else {
            ++result.skipped;
        }
    }
    return pruned;
}
