# Regression tests

Configure the [Windows build](../docs/build.md) first, then run the tests from the repository root.
```powershell
xmake build callback-slots-test
xmake build runtime-support-test
xmake build settings-test
xmake build diagnostic-logging-test
xmake build cosave-prune-test
xmake build startup-pruning-test
xmake build save-search-test
xmake build usvfs-search-test
python tests/run_search_tests.py .build/windows/x64/release
python -m unittest discover -s tests/cosaves -v
```
On Linux, pass `--wine /path/to/wine`. Add `--usvfs /path/to/mo2` for portable-profile and fallback tests with `usvfs_x64.dll` and `usvfs_proxy_x64.exe`. The runner creates a disposable VFS. Synthetic searches compare order, names, attributes, sizes, and timestamps on Wine and native Windows with up to 10,507 orphans.

Pass `--deck-fixtures tests/fixtures/deck` to include private samples. The runner verifies their manifest, copies them into a disposable profile, and checks that cleanup preserves each paired save and cosave. Keep private fixtures and traces out of Git. Distinguish a compiler check, a local Wine run, and in-game verification when reporting results.

Pass `--startup-scale` to check startup dispatch, cancellation, and process exit with 10,507 stale orphans. The tests run the plugin's real Windows thread-pool worker against disposable profiles. They also check that a second save extends the quiet period, cancellation invalidates a pending sweep, and another save re-arms it. The runner verifies save pairs after each process exits.

## Linux checks without Wine

Use a C++23 compiler and Python 3 to check callback dispatch, runtime selection, save path matching, and the offline cosave tool from the repository root.
```sh
mkdir -p .build
g++ -std=c++23 -Wall -Wextra -Werror -Iinclude tests/menu/callback_slots.cpp -o .build/callback-slots-test
.build/callback-slots-test
g++ -std=c++23 -Wall -Wextra -Werror -Iinclude tests/plugin/runtime_support.cpp -o .build/runtime-support-test
.build/runtime-support-test
python3 -m unittest discover -s tests/cosaves -v
```
`tools/package.py` also runs on Linux with an existing Windows DLL. The plugin build and Windows filesystem regressions still require the toolchain described above.
