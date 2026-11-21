#include <Configuration/DiagnosticLogging.h>

#include <filesystem>
#include <fstream>
#include <optional>

namespace
{
    std::optional<std::filesystem::path> logDirectory;
}

namespace SKSE::log
{
    std::optional<std::filesystem::path> log_directory()
    {
        return logDirectory;
    }
}

int main(int argc, char* argv[])
{
    using namespace SwiftSaveMenus;
    if (argc != 2 || DiagnosticLogger() || SetDiagnosticLogging(true)) {
        return 1;
    }

    logDirectory = argv[1];
    const auto path = *logDirectory / "SwiftSaveMenus.log";
    if (std::filesystem::exists(path) || !SetDiagnosticLogging(true) || !DiagnosticLogger()) {
        return 1;
    }
    DiagnosticLogger()->info("first enable");
    if (!SetDiagnosticLogging(false) || DiagnosticLogger() || !SetDiagnosticLogging(true) || !DiagnosticLogger()) {
        return 1;
    }
    DiagnosticLogger()->info("second enable");

    std::ifstream log(path);
    const std::string contents{ std::istreambuf_iterator<char>(log), {} };
    return contents.contains("first enable") && contents.contains("second enable") ? 0 : 1;
}
