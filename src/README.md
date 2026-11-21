# Plugin source

Put headers in domain directories under `include/` and implementations in domain directories under `src/`. Declare at most one class interface per header.

| Area | Responsibility |
| --- | --- |
| `Plugin/` | SKSE entry point and lifecycle dispatch |
| `Configuration/` | Plugin INI and live diagnostic logging |
| `Search/` | Directory hooks, verified MO2 profile paths, and Windows search semantics |
| `Menu/` | Scaleform timing and settings UI registration |
| `Cosaves/` | Background cleanup and protection of paired saves |

Keep save search independent of cleanup. Preserve Skyrim's search when the runtime or save path cannot be verified. Use the pinned CommonLibSSE-NG types and relocations. Check hook signatures, callers, and patch sites against the target executable before enabling a hook.
