# Build and packaging

Use xmake 3.0.1 or later, MSVC, and Python 3. Run these commands from the repository root.

This checkout owns its native Windows build and test recipes. Install the MSVC x64 C++ toolchain and a Windows SDK. xmake resolves CommonLibSSE-NG's declared packages from its package repositories. Wine can run the resulting test executables on Linux. The optional USVFS cases take MO2 binaries as a test input, as described in the regression guide.

```powershell
git submodule update --init --recursive
xmake f -p windows -a x64 -m release --skyrim_se=y --skyrim_ae=y --skyrim_vr=y -o .build -y
xmake build SwiftSaveMenus
python tools/package.py .build/windows/x64/release/SwiftSaveMenus.dll .build/SwiftSaveMenus-0.1.0.zip
```

Use the [regression guide](../tests/README.md) before packaging a candidate. Put verified MO2 test ZIPs in Downloads on the Deck. Installing into the active profile requires deployment authorization.

The build enables SE, AE, and VR in CommonLibSSE-NG. The save search uses the executable's Windows imports. The optional Scaleform timing probes use verified callback locations for Skyrim `1.6.1170`. In-game verification covers that runtime.

Keep the version checks and the player-facing requirements in sync when adding a Skyrim release. Confirm the matching SKSE build and check the package's load guard. [Runtime support table](../include/Plugin/RuntimeSupport.h) · [Requirements](configuration.md#requirements)

## CI and release

GitHub Actions links the checksum-pinned CommonLib binary in one Windows build job and uploads a candidate ZIP with its SHA-256 and test executables. Parallel jobs run the native regressions, check the game/SKSE version policy, and exercise the packaged DLL's rejection of unsupported launches. These checks run without Skyrim. A version tag matching `xmake.lua` publishes the same ZIP after both jobs pass.

Tag a commit reachable from `main`. The release job checks ancestry before downloading the candidate. It holds the workflow's write permission. The other jobs use read-only tokens and discard checkout credentials before running build or test commands. When updating action pins, review the upstream revision and keep its version comment in sync.

CI uses the hosted Visual Studio 2026 image's current MSVC toolset. The linker must support the prebuilt library's MSVC 14.51 objects. The build checks the dependency revision and archive hash before staging the library. A missing or incompatible library fails the build without a source rebuild. Use the manual `source` mode when a dependency update needs a from-source check.

Swift Save Menus uses GPL-3.0-or-later. Each release includes the license and attribution in the mod ZIP and a source ZIP with the pinned CommonLibSSE-NG and OpenVR sources. The source packager reads symlink targets from Git so a Windows checkout can preserve them in the archive. Use the sources for the package versions declared by the build. [DirectXMath 2024.02](https://github.com/microsoft/DirectXMath/tree/feb2024) · [DirectXTK 24.2.0](https://github.com/microsoft/DirectXTK/tree/feb2024) · [spdlog 1.16.0](https://github.com/gabime/spdlog/tree/v1.16.0) · [rapidcsv 8.92](https://github.com/d99kris/rapidcsv/tree/v8.92)

### Nexus Mods upload

The Nexus job uses the same tested mod ZIP after GitHub publishes a release. The `nexus` environment permits release tags and holds `NEXUSMODS_API_KEY`, `NEXUSMODS_FILE_ID` (the API File ID of the existing main file), and `NEXUSMODS_MOD_ID` (the unique mod ID). The job verifies the archive hash and checks the chain on Nexus before uploading.

The job uploads each tagged release to the existing main chain and enables the download requirements popup. Requirements are managed on the Nexus mod page. [Nexus upload action](https://github.com/Nexus-Mods/upload-action) · [Nexus listing copy](nexus/description.bbcode)
