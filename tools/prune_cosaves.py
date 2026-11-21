import argparse
import os
import stat
import tempfile
import time
from pathlib import Path


def file_identity(info: os.stat_result):
    return info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def inventory(folder: Path, cutoff_ns: int):
    protected = set()
    cosaves = {}
    ambiguous = set()
    with os.scandir(folder) as entries:
        for entry in entries:
            path = Path(entry.name)
            stem, suffix = path.stem.casefold(), path.suffix.casefold()
            if suffix == ".ess":
                protected.add(stem)
            elif suffix == ".tmp":
                protected.add(stem)
                if Path(stem).suffix in {".ess", ".skse"}:
                    protected.add(Path(stem).stem)
            elif suffix == ".skse":
                if stem in cosaves:
                    ambiguous.add(stem)
                cosaves[stem] = entry

    candidates = {}
    for stem, entry in cosaves.items():
        if stem in protected or stem in ambiguous:
            continue
        # Windows directory entries omit the file identity used by the final check.
        identity = (folder / entry.name).lstat()
        if stat.S_ISREG(identity.st_mode) and identity.st_mtime_ns <= cutoff_ns:
            candidates[entry.name] = file_identity(identity)
    return candidates


def prune(folder: Path, cutoff_ns: int, apply: bool = False, *, offline: bool = False):
    if apply and not offline:
        raise ValueError("Close Skyrim and MO2 before applying cleanup")
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError("Select the physical save directory")
    folder = folder.resolve(strict=True)
    planned = inventory(folder, cutoff_ns)
    if not apply or not planned:
        return len(planned), 0, None

    quarantine = Path(tempfile.mkdtemp(prefix=f"{folder.name}-cosave-recovery-", dir=folder.parent))
    confirmed = inventory(folder, cutoff_ns)
    eligible = sorted(name for name, candidate in planned.items() if confirmed.get(name) == candidate)
    if not eligible:
        quarantine.rmdir()
        return len(planned), 0, None

    moved = 0
    try:
        for name in eligible:
            source = folder / name
            if file_identity(source.lstat()) != planned[name]:
                continue
            source.rename(quarantine / name)
            moved += 1
    except OSError as error:
        raise OSError(f"Moved {moved} to {quarantine} before stopping: {error}") from error
    return len(planned), moved, quarantine


def main() -> None:
    parser = argparse.ArgumentParser(description="Find stale SKSE cosaves in one physical save directory.")
    parser.add_argument("folder", type=Path)
    parser.add_argument("--apply", action="store_true", help="Move eligible cosaves to a sibling recovery directory")
    parser.add_argument("--game-closed", action="store_true", help="Confirm Skyrim and MO2 are closed")
    args = parser.parse_args()
    yesterday = time.time_ns() - 24 * 60 * 60 * 1_000_000_000
    try:
        planned, moved, recovery = prune(args.folder, yesterday, args.apply, offline=args.game_closed)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Cleanup stopped: {error}\n")
    print(f"Stale cosaves: {planned}")
    if recovery:
        print(f"Moved {moved} to {recovery}")


if __name__ == "__main__":
    main()
