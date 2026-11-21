#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Search/BatchedSearch.h>

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{
    using Entries = std::vector<WIN32_FIND_DATAA>;
    using Clock = std::chrono::steady_clock;

    struct Result
    {
        Entries entries;
        double milliseconds;
    };

    void Require(bool condition, const char* message)
    {
        if (!condition) {
            std::fprintf(stderr, "%s (last error %lu)\n", message, GetLastError());
            std::exit(EXIT_FAILURE);
        }
    }

    Result Run(const char* pattern, bool batched, const char* profileMode = nullptr)
    {
        WIN32_FIND_DATAA data{};
        const auto start = Clock::now();
        bool usedPhysical = false;
        auto handle = batched ? SaveLoadFix::FindFirstBatched(pattern, &data, profileMode ? &usedPhysical : nullptr) : FindFirstFileA(pattern, &data);
        Require(handle && handle != INVALID_HANDLE_VALUE, "Search failed to open");
        Require(usedPhysical == (profileMode && std::strcmp(profileMode, "physical") == 0), "Unexpected search path selected");
        Entries entries;
        do {
            entries.push_back(data);
        } while (batched ? SaveLoadFix::FindNextBatched(handle, &data).value_or(FALSE) : FindNextFileA(handle, &data));
        Require(GetLastError() == ERROR_NO_MORE_FILES, "Expected normal end of enumeration");
        Require(batched ? SaveLoadFix::FindCloseBatched(handle).value_or(FALSE) : FindClose(handle), "Close failed");
        return { std::move(entries), std::chrono::duration<double, std::milli>(Clock::now() - start).count() };
    }

    bool SameEntry(const WIN32_FIND_DATAA& a, const WIN32_FIND_DATAA& b, bool compareAccessTime)
    {
        return std::strcmp(a.cFileName, b.cFileName) == 0 &&
               std::strcmp(a.cAlternateFileName, b.cAlternateFileName) == 0 &&
               a.dwFileAttributes == b.dwFileAttributes && a.dwReserved0 == b.dwReserved0 &&
               a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
               CompareFileTime(&a.ftCreationTime, &b.ftCreationTime) == 0 &&
               (!compareAccessTime || CompareFileTime(&a.ftLastAccessTime, &b.ftLastAccessTime) == 0) &&
               CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
    }

    void CheckFreshSearches(const char* wildcard)
    {
        namespace fs = std::filesystem;
        const auto folder = fs::path(wildcard).parent_path();
        std::ofstream(folder / "autosave7.ess") << "old";
        auto first = Run(wildcard, true);
        Require(first.entries.size() == 1 && first.entries.front().nFileSizeLow == 3, "Initial save was missing");

        std::ofstream(folder / "autosave7.ess") << "overwritten";
        auto replaced = Run(wildcard, true);
        Require(replaced.entries.size() == 1 && replaced.entries.front().nFileSizeLow == 11, "Replacement metadata was stale");

        fs::remove(folder / "autosave7.ess");
        std::ofstream(folder / "autosave8.ESS") << "rotation";
        fs::current_path(folder);
        auto rotated = Run("*.ess", true);
        Require(rotated.entries.size() == 1 && std::strcmp(rotated.entries.front().cFileName, "autosave8.ESS") == 0,
            "Search returned an old autosave after rotation");

        fs::remove(folder / "autosave8.ESS");
        WIN32_FIND_DATAA data{};
        Require(SaveLoadFix::FindFirstBatched(wildcard, &data) == INVALID_HANDLE_VALUE &&
            GetLastError() == ERROR_FILE_NOT_FOUND, "Deleted save still appeared");
    }

    void CheckConcurrentSearches(const char* wildcard)
    {
        WIN32_FIND_DATAA first{}, second{};
        const auto a = SaveLoadFix::FindFirstBatched(wildcard, &first);
        const auto b = SaveLoadFix::FindFirstBatched(wildcard, &second);
        Require(a && b && a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE && a != b,
            "Could not open independent search handles");
        Require(SameEntry(first, second, true), "Concurrent searches disagreed on the first result");
        Require(SaveLoadFix::FindNextBatched(a, &first).value_or(FALSE), "First search failed to advance");
        Require(SaveLoadFix::FindNextBatched(b, &second).value_or(FALSE), "Second search failed to advance");
        Require(SameEntry(first, second, true), "Concurrent searches shared a cursor");
        Require(SaveLoadFix::FindCloseBatched(a).value_or(FALSE), "First close failed");
        Require(SaveLoadFix::FindNextBatched(b, &second).value_or(FALSE), "Closing one search invalidated the other");
        Require(SaveLoadFix::FindCloseBatched(b).value_or(FALSE), "Second close failed");
    }
}

int main(int argc, char** argv)
{
    Require(argc >= 3 && argc <= 5, "Usage: save-search-test PATTERN EXPECTED_COUNT [ROUNDS] [physical|virtual|redirected]");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 3 && std::strcmp(argv[2], "mutations") == 0) {
        CheckFreshSearches(argv[1]);
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[2], "concurrent") == 0) {
        CheckConcurrentSearches(argv[1]);
        return 0;
    }
    const auto rounds = argc >= 4 ? std::strtoul(argv[3], nullptr, 10) : 2;
    Require(rounds > 0, "Expected at least one round");
    WIN32_FIND_DATAA data{};
    const auto expectedCount = std::strtoul(argv[2], nullptr, 10);
    const char* reason{};
    Require(!SaveLoadFix::FindFirstBatched("Saves\\*.skse", &data, nullptr, nullptr, &reason) &&
        std::string_view(reason) == "unsupported_query", "Unexpected filter accepted or wrong reason");
    Require(!SaveLoadFix::FindFirstBatched("Saves\\missing\\*.ess", &data, nullptr, nullptr, &reason) &&
        std::string_view(reason) == "directory_open_failed", "Missing directory accepted or wrong reason");
    if (!expectedCount) {
        Require(FindFirstFileA(argv[1], &data) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND,
            "Empty fixture returned entries");
        Require(SaveLoadFix::FindFirstBatched(argv[1], &data) == INVALID_HANDLE_VALUE &&
            GetLastError() == ERROR_FILE_NOT_FOUND, "Batched empty fixture did not report file not found");
        return 0;
    }
    for (unsigned long i = 0; i < rounds; ++i) {
        auto original = Run(argv[1], false);
        Require(original.entries.size() == expectedCount, "Fixture count differs");
        std::printf("%zu entries, original %.3f ms\n", original.entries.size(), original.milliseconds);
        auto batch = Run(argv[1], true, argc == 5 ? argv[4] : nullptr);
        Require(original.entries.size() == batch.entries.size(), "Entry count differs");
        // Physical mapping validation must open a file before retrying this search.
        const bool compareAccessTime = argc != 5 || std::strcmp(argv[4], "redirected") != 0;
        for (std::size_t j = 0; j < original.entries.size(); ++j) {
            const auto& a = original.entries[j];
            const auto& b = batch.entries[j];
            if (!SameEntry(a, b, compareAccessTime)) {
                std::fprintf(stderr, "Entry %zu: %s [%s] %lu %lu vs %s [%s] %lu %lu; time comparisons %ld %ld %ld\n", j,
                    a.cFileName, a.cAlternateFileName, a.dwFileAttributes, a.nFileSizeLow,
                    b.cFileName, b.cAlternateFileName, b.dwFileAttributes, b.nFileSizeLow,
                    CompareFileTime(&a.ftCreationTime, &b.ftCreationTime), CompareFileTime(&a.ftLastAccessTime, &b.ftLastAccessTime),
                    CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime));
                Require(false, "Entry order or metadata differs");
            }
        }
        std::printf("%zu equivalent entries, batch %.3f ms\n", batch.entries.size(), batch.milliseconds);
    }
}
