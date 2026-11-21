set_xmakever("3.0.1")
set_project("SwiftSaveMenus")
set_version("0.1.0")
set_languages("c++23")
add_rules("mode.release", "mode.debug")
includes("lib/commonlibsse-ng")
add_includedirs("include")

local search_sources = { "src/Search/BatchedSearch.cpp", "src/Search/PortableProfile.cpp" }

target("SwiftSaveMenus")
    set_kind("shared")
    set_license("GPL-3.0-or-later")
    add_rules("commonlibsse-ng.plugin", {
        name = "Swift Save Menus",
        author = "andypenno",
        description = "Faster Skyrim Save and Load menus"
    })
    add_files("src/Plugin/Plugin.cpp", "src/Configuration/Settings.cpp", "src/Configuration/DiagnosticLogging.cpp",
        "src/Cosaves/Prune.cpp", "src/Cosaves/StartupPruning.cpp", "src/Search/DirectoryHooks.cpp",
        "src/Menu/MenuTiming.cpp", "src/Menu/SettingsMenu.cpp", table.unpack(search_sources))
target_end()

target("settings-test")
    set_default(false)
    set_kind("binary")
    set_runtimes("MT")
    add_files("tests/settings/settings.cpp", "src/Configuration/Settings.cpp")
target_end()

target("diagnostic-logging-test")
    set_default(false)
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_files("tests/settings/diagnostic_logging.cpp", "src/Configuration/DiagnosticLogging.cpp")
target_end()

target("cosave-prune-test")
    set_default(false)
    set_kind("binary")
    set_runtimes("MT")
    add_files("tests/cosaves/prune.cpp", "src/Cosaves/Prune.cpp")
target_end()

target("startup-pruning-test")
    set_default(false)
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_files("tests/cosaves/startup.cpp", "src/Cosaves/StartupPruning.cpp", "src/Cosaves/Prune.cpp",
        "src/Search/PortableProfile.cpp")
target_end()

target("save-search-test")
    set_default(false)
    set_kind("binary")
    set_runtimes("MT")
    add_files("tests/search/save_search.cpp", table.unpack(search_sources))
target_end()

target("usvfs-search-test")
    set_default(false)
    set_kind("binary")
    add_files("tests/search/usvfs_search.cpp")
target_end()

target("callback-slots-test")
    set_default(false)
    set_kind("binary")
    add_files("tests/menu/callback_slots.cpp")
target_end()

target("runtime-support-test")
    set_default(false)
    set_kind("binary")
    add_files("tests/plugin/runtime_support.cpp")
target_end()
