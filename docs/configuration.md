# Configuration and recovery

## Requirements

The same Swift Save Menus ZIP covers all Skyrim runtimes listed below. The plugin checks the exact game version and requires a matching SKSE build in the same major version at or above the listed minimum. Install the Address Library that matches your game version. Skyrim VR uses SKSE VR and VR Address Library. [SKSE downloads](https://skse.silverlock.org/) · [Address Library for SE and AE](https://www.nexusmods.com/skyrimspecialedition/mods/32444) · [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101)

| Edition | Skyrim | Minimum matching SKSE |
| --- | --- | --- |
| Special Edition | 1.5.97 | 2.0.20 |
| Anniversary Edition (Steam) | 1.7.104 | 2.3.1 |
| Anniversary Edition (GOG) | 1.6.1179 | 2.2.6 |
| VR | 1.4.15 | 2.0.12 |

Archived AE builds are also supported by the loader.

| Skyrim | Minimum matching SKSE |
| --- | --- |
| 1.6.317 | 2.1.0 |
| 1.6.318 | 2.1.1 |
| 1.6.323 | 2.1.3 |
| 1.6.342 | 2.1.4 |
| 1.6.353 | 2.1.5 |
| 1.6.629 | 2.2.0 |
| 1.6.640, 1.6.659 | 2.2.2 |
| 1.6.1130 | 2.2.4 |
| 1.6.1170 | 2.2.6 |
| 1.7.99 | 2.3.0 |

## Settings

Edit `SKSE/Plugins/SaveLoadFix.ini` before launching Skyrim, or open **Swift Save Menus / Settings** if SKSE Menu Framework is installed. Changes made in the game apply immediately and persist to the INI. Enabling SKSE Cosave pruning queues a background sweep as soon as the plugin has verified the save folder. The backup option appears while pruning is enabled. Changing it restarts the sweep in the selected mode.

The ZIP uses the existing `SaveLoadFix.dll` and `SaveLoadFix.ini` filenames so an upgrade can reuse the installed settings. Keep your edited INI when upgrading.

| Setting | Default | Behavior |
| --- | --- | --- |
| `EnableLogging` | `0` | Enable debug logging to record search and menu timings in `SaveLoadFix.log` beside `skse64.log`. |
| `EnableCosavePruning` | `1` | Queue a background sweep after verifying Skyrim's standard save folder or a portable MO2 profile. Saves cancel the active sweep and arm another after 30 seconds without a save. Loading or turning pruning off cancels pending work. An operation already in progress can finish. |
| `BackupPrunedCosaves` | `0` | Set to `1` to back up eligible SKSE Cosaves. The default deletes them permanently. |
| `EnableSettingsMenu` | `1` | Set to `0` in the INI before launch to hide the Swift Save Menus page in SKSE Menu Framework. |

Cleanup skips cosaves younger than 24 hours and protects matching saves, temporary files, ambiguous names, and reparse points. Matches ignore case. Windows folders marked case-sensitive are skipped. Backup mode moves eligible cosaves to `SaveLoadFix-cosave-recovery` beside the profile's `saves` folder and preserves existing recovery files. Pause external sync or restore jobs during startup cleanup. Keep Engine Fixes' cosave cleaner disabled if you want recovery copies.

## Offline cleanup and restore

Close Skyrim and MO2 and pause sync or restore jobs on the save folder. The first command reports eligible cosaves. The second moves them to a new sibling recovery folder.

```sh
python tools/prune_cosaves.py /path/to/profile/saves
python tools/prune_cosaves.py /path/to/profile/saves --apply --game-closed
```

To restore a cosave, move it back from the recovery folder with the game closed.

## Reporting a bug

Enable debug logging and reproduce the issue. For startup problems, set `EnableLogging=1` in the INI before launching Skyrim. Copy `SaveLoadFix.log` and `skse64.log` from the SKSE log folder before starting another session with logging enabled. For crashes, include the crash report if available. Describe the menu or action that triggered the issue and include your mod manager and its save-folder settings. Review logs for personal paths before sharing.
