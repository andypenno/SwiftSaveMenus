# Skyrim save pipeline

The runtime-independent search hooks use Windows imports. The game-specific timing evidence in this guide comes from Skyrim `1.6.1170` with SKSE `2.2.8` and MO2 profile-local saves. Revalidate addresses and signatures before extending the timing probes to another executable.

## Directory discovery

Skyrim searches for `.ess` entries using its `FindFirstFileA`, `FindNextFileA`, and `FindClose` imports. In the captured pause-menu flow, one request caused two searches. The timed `BGSSaveLoadManager::PopulateSaveList()` entry was never called in that flow. Per-entry metadata reads and screenshot work followed enumeration.

MO2 maps Skyrim's `__MO_Saves` directory to the selected profile's `saves` folder. USVFS hooks `FindFirstFileExW` and `NtQueryDirectoryFile`. Its merged virtual listing can query individual mapped entries. Proton's directory work includes long-name conversion and generating DOS short names, even though Skyrim's result list only contains `.ess` files. The physical directory can therefore matter even when unrelated `.skse` files are filtered out. [USVFS implementation](https://github.com/ModOrganizer2/usvfs/blob/master/src/usvfs_dll/hooks/ntdll.cpp) · [Proton directory implementation](https://github.com/ValveSoftware/wine/blob/proton_11.0/dlls/ntdll/unix/file.c)

The plugin batches `*.ess` queries through `NtQueryDirectoryFile` on Windows and Wine, including custom save folders. A valid folder with no matching saves returns an empty result directly. Unsupported or failed batch queries use the original Windows API.

A disposable default Proton 11 prefix passed the plain-directory regression without MO2. Its 2,048-save fixture took 34 ms through the original search and 31 ms through batching. The fixture with 252 saves and 10,507 orphan SKSE Cosaves took about 26 ms through either path. These timings cover enumeration only.

The portable-profile shortcut removed the expensive virtual enumeration in the game. It checks the backing destination for real `.ess` files and uses file identity to validate the selected profile mapping. Skyrim VR uses the same `__MO_Saves` mapping with `SkyrimVR.ini`. Unverified layouts use the general batching code against the active directory. The Wine regression verifies the names, order, sizes, attributes, and write times. It ignores the access timestamp in the mismatched-profile fixture because opening a file to verify its identity can update it. [MO2 Skyrim VR plugin](https://github.com/ModOrganizer2/modorganizer-game_skyrimvr/blob/15995fc10b66f2b48ff510f85a7c96bf8f9c8c7d/src/gameskyrimvr.cpp)

## Menu timing

Main-menu `CharacterSelected` and pause-menu `SAVE` and `LOAD` are distinct starting points. `PrepSaveGameScreenshot` is a proxy for the list appearing. The Clean Menu start-menu movie and SkyUI Flashing Savegames Fix journal movie both contain `onSaveLoadBatchComplete` and `OnSaveListBatchAdded`. SkyUI's save panel handles the former with an initial-update boolean and dispatches `saveListPopulated` during that initial update. Verify call sites in the running Steam executable because its on-disk menu code is packed. [SkyUI save panel](https://github.com/schlangster/skyui/blob/master/src/Common/SaveLoadPanel.as)

In the running Steam `1.6.1170` executable, the `onSaveLoadBatchComplete` string at RVA `0x19063F8` has a native call site at `0x97A476`. Its target at `0xFBE900` takes a movie pointer, callback name, and response args. It calls the response args virtual method in slot 1 to retrieve the value array, then writes the callback name into its first slot. The caller constructs a boolean and two numeric values. The two numbers' meaning and the terminal batch condition still need verification before reporting full-list timing.

With debug logging enabled, the batch probe reports when the native dispatch returns and records the boolean and both numbers. It only attaches on the verified executable and expected call bytes. It correlates batches with the movie that started the menu trace. Measure actual selection and the terminal batch in game before using these timings as UI readiness.

Each menu search reads fresh directory metadata. Replacing a save in place can change its size or write time without changing the directory entry name. Autosave rotation can add and remove entries between searches. The regression checks both cases and deletion between requests. A shared listing needs a verified invalidation boundary before it can replace these reads.

An ASCII filename conversion prototype was slower than the existing Windows conversion in alternating Wine runs on 2,048-save and 10,507-orphan fixtures. Keep the conversion so accented, non-ANSI, and DOS short names retain the same handling.

### Performance reference

| Steam Deck measurement | Original search | With the portable profile fix |
| --- | ---: | ---: |
| Synthetic portable MO2 fixture, directory search | 5,021.095 ms | 11.088 ms |
| In-game pause-menu Load, first screenshot request | 31,416.801 ms | 582.953 ms |

The synthetic result is one paired observation from the `USVFS fixture: verified portable profile` case in `tests/run_search_tests.py`. It compares the original and optimized searches in the same run using 252 saves, matching SKSE Cosaves, and 10,507 orphaned SKSE Cosaves. Both searches returned equivalent entries with all orphaned files left in place. Pass `--usvfs` to run this case. [Regression instructions](../tests/README.md)

The in-game values come from two diagnostic sessions on Skyrim `1.6.1170` with MO2 profile-local saves. Each measures from the pause-menu `LOAD` callback to the first `PrepSaveGameScreenshot` request. Both baseline directory scans in that interval returned 252 entries. The second trace used the verified physical MO2 path and did not log an equivalent entry count. These observations use the same endpoint but came from separate sessions with different diagnostic builds. Full-list interactivity and actual save or load duration use different endpoints.

## Save writers and cosaves

SKSE dispatches `kMessage_SaveGame` before calling its hooked save target. `kMessage_PreLoadGame` precedes the load target. `kMessage_NewGame` reports creation of a new game. Each message publishes cancellation without waiting for the worker. Each mutation checks cancellation after verifying the open SKSE Cosave and its possible partners. An operation that has passed the last check can still finish. Mod-specific writers and external sync can bypass these SKSE messages. [SKSE save/load hooks](https://github.com/ianpatt/skse64/blob/master/skse64/Hooks_SaveLoad.cpp)

The cosave regression adds a matching save and a temporary cosave after both inventory passes. The per-file checks must keep both paired cosaves in place while still recovering the unmatched one. A writer that creates a partner after the final check remains outside what this test verifies.

The pruning worker uses metadata from enumeration and mutates the open SKSE Cosave handle. The final checks for missing partners and temporary files dominate large sweeps. The in-game stress test deleted 10,509 orphans and preserved every file in 253 save pairs. The normal-play test covered a cancelled sweep, manual save, autosave rotation, and deferred cleanup.

The worker regression runs the actual thread-pool dispatch in a disposable Windows process. It checks queueing with and without a backlog, disabling pruning after the first deletion, re-enabling pruning in backup mode, and exit while work remains. The pruning regression holds an orphan open for writing and checks that deletion skips it until the writer closes the handle. [Regression tests](../tests/README.md)

A save event restarts a one-shot 30-second quiet timer when pruning is enabled. This gives rotating autosaves a later sweep and avoids rescanning during idle play. The timer starts the same worker after the quiet period. A new save supersedes the earlier timer request, and load and disable events cancel it. Windows background thread priority reduces competition with Skyrim when supported. The quiet period is a delay from the save notification. Writer coverage still depends on which mods send that notification and how they handle asynchronous writes.

Directory-relative `NtQueryAttributesFile` checks preserved fixture results but did not reduce time in the 1,000-orphan Wine comparison. Each candidate still uses the four explicit late-file checks.
