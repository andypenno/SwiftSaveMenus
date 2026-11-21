#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) {
            std::fprintf(stderr, "%s (last error %lu)\n", message, GetLastError());
            std::exit(EXIT_FAILURE);
        }
    }

    template <class Function>
    Function Export(HMODULE module, const char* name)
    {
        auto function = reinterpret_cast<Function>(GetProcAddress(module, name));
        Require(function != nullptr, name);
        return function;
    }
}

int wmain(int argc, const wchar_t* argv[])
{
    Require(argc == 5 || argc == 6, "Usage: usvfs-search-test TEST_EXE SOURCE TARGET EXPECTED_COUNT [physical|virtual|redirected]");
    const auto dll = LoadLibraryW(L"usvfs_x64.dll");
    Require(dll != nullptr, "Could not load usvfs_x64.dll");
    const auto createParameters = Export<void* (WINAPI*)()>(dll, "usvfsCreateParameters");
    const auto setName = Export<void (WINAPI*)(void*, const char*)>(dll, "usvfsSetInstanceName");
    const auto create = Export<BOOL (WINAPI*)(const void*)>(dll, "usvfsCreateVFS");
    const auto freeParameters = Export<void (WINAPI*)(void*)>(dll, "usvfsFreeParameters");
    const auto initLogging = Export<void (WINAPI*)(bool)>(dll, "usvfsInitLogging");
    const auto link = Export<BOOL (WINAPI*)(LPCWSTR, LPCWSTR, unsigned)>(dll, "usvfsVirtualLinkDirectoryStatic");
    const auto launch = Export<decltype(&CreateProcessW)>(dll, "usvfsCreateProcessHooked");
    const auto disconnect = Export<void (WINAPI*)()>(dll, "usvfsDisconnectVFS");

    const auto parameters = createParameters();
    Require(parameters != nullptr, "Could not allocate usvfs parameters");
    const auto name = "SaveLoadFixTest" + std::to_string(GetCurrentProcessId());
    setName(parameters, name.c_str());
    initLogging(false);
    Require(create(parameters), "Could not create test VFS");
    freeParameters(parameters);
    constexpr unsigned createTarget = 4;
    constexpr unsigned recursive = 8;
    Require(link(argv[2], argv[3], createTarget | recursive), "Could not map save directory");

    std::wstring command = std::wstring(L"\"") + argv[1] + L"\" \"" + argv[3] + L"\\*.ess\" " + argv[4];
    if (argc == 6) {
        command += std::wstring(L" 1 ") + argv[5];
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Require(launch(argv[1], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process),
        "Could not launch test through usvfs");
    Require(WaitForSingleObject(process.hProcess, 90000) == WAIT_OBJECT_0, "USVFS test did not finish");
    DWORD exitCode;
    Require(GetExitCodeProcess(process.hProcess, &exitCode), "Could not read test result");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    disconnect();
    return exitCode;
}
