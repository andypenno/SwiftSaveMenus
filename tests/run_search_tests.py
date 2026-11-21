import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from os import utime

from fixtures import deck_samples
from fixtures.synthetic import create


def main():
    root = Path(__file__).resolve().parents[1]
    for folder in (root / "src", root / "include"):
        misplaced = sorted(path.name for path in folder.iterdir() if path.is_file() and path.suffix in {".cpp", ".h"})
        if misplaced:
            raise ValueError(f"Put C++ files under a domain directory in {folder.name}: {', '.join(misplaced)}")
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path, help="Directory containing the Windows test executables")
    parser.add_argument("--wine", help="Wine executable for running the tests")
    parser.add_argument("--usvfs", type=Path, help="Directory containing MO2's USVFS DLL and proxy")
    parser.add_argument("--deck-fixtures", type=Path, help="Private Deck fixture directory with a manifest")
    parser.add_argument("--startup-scale", action="store_true", help="Use 10,507 orphans for startup worker tests")
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    wine = [args.wine] if args.wine else []

    def windows(path):
        return "Z:" + str(path).replace("/", "\\") if wine else str(path)

    def run(executable, *arguments):
        subprocess.run([*wine, str(executable), *arguments], check=True, timeout=100)

    with tempfile.TemporaryDirectory(prefix="swiftsavemenus-tests-") as tmp:
        root = Path(tmp)
        create(root)
        run(build / "settings-test.exe", windows(root / "settings.ini"))
        run(build / "diagnostic-logging-test.exe", windows(root))
        run(build / "cosave-prune-test.exe", windows(root))
        for mode in ("empty", "after-save", "cancel", "exit"):
            profile = root / f"startup-{mode}"
            saves = profile / "Saves"
            saves.mkdir(parents=True)
            (profile / "Elsewhere").mkdir()
            pairs = {saves / f"Save{index:03}.{ext}": f"pair {index} {ext}".encode()
                     for index in range(252) for ext in ("ess", "skse")}
            old = time.time() - 48 * 3600
            for path, data in pairs.items():
                path.write_bytes(data)
                utime(path, (old, old))
            size = {"empty": 0, "after-save": 3}.get(mode, 10507 if args.startup_scale else 128)
            for index in range(size):
                path = saves / f"Orphan{index:05}.skse"
                path.touch()
                utime(path, (old, old))
            print(f"Startup worker fixture: {mode}, {size} orphans", flush=True)
            run(build / "startup-pruning-test.exe", windows(profile), mode)
            if any(path.read_bytes() != data for path, data in pairs.items()):
                raise ValueError("Startup worker changed a paired save or cosave")
        run(build / "callback-slots-test.exe")
        run(build / "runtime-support-test.exe")
        search = build / "save-search-test.exe"
        for fixture, count in (("empty", 0), ("cosaves-only", 0), ("paired", 252), ("orphans", 252), ("large", 2048)):
            print(f"Search fixture: {fixture}", flush=True)
            run(search, windows(root / fixture / "__MO_Saves" / "*.ess"), str(count))
        standard = windows(root / "Skyrim Special Edition" / "Saves" / "*.ess")
        print("Search fixture: standard folder with mixed extensions, Unicode, and independent cursors", flush=True)
        run(search, standard, "6")
        run(search, standard, "concurrent")
        print("Search fixture: save replacement, autosave rotation, and deletion", flush=True)
        custom = root / "CustomSaveDirectory"
        custom.mkdir()
        run(search, windows(custom / "*.ess"), "mutations")

        if args.deck_fixtures:
            profile = root / "deck"
            saves = profile / "saves"
            manifest = deck_samples.prepare(args.deck_fixtures, saves)
            print("Deck fixtures: three save pairs and one orphan", flush=True)
            run(search, windows(saves / "*.ess"), "3")
            run(build / "cosave-prune-test.exe", windows(profile), "deck-fixtures")
            deck_samples.verify_recovery(saves, manifest)

        if args.usvfs:
            mo2 = root / "MO2"
            mo2.mkdir()
            for binary, source in (("usvfs_x64.dll", args.usvfs), ("usvfs_proxy_x64.exe", args.usvfs),
                                   ("usvfs-search-test.exe", build), ("save-search-test.exe", build)):
                shutil.copy2(source / binary, mo2 / binary)
            source = root / "orphans" / "__MO_Saves"
            launcher, search = (mo2 / name for name in ("usvfs-search-test.exe", "save-search-test.exe"))
            print("USVFS fixture: real and shadowed entries", flush=True)
            run(launcher, windows(search), windows(source), windows(root / "redirected" / "__MO_Saves"), "253")

            profile = mo2 / "profiles" / "Default"
            profile.mkdir(parents=True)
            source.rename(profile / "saves")
            (mo2 / "ModOrganizer.exe").touch()
            (mo2 / "ModOrganizer.ini").write_text("[General]\ngameName=Skyrim Special Edition\nselected_profile=@ByteArray(Default)\n")
            (profile / "settings.ini").write_text("[General]\nLocalSaves=true\n")
            (profile / "skyrimcustom.ini").write_text("[General]\nsLocalSavePath=__MO_Saves\\\n")
            target = root / "portable" / "__MO_Saves"
            target.mkdir(parents=True)
            print("USVFS fixture: verified portable profile", flush=True)
            run(launcher, windows(search), windows(profile / "saves"), windows(target), "252", "physical")
            overlay = root / "overlay" / "__MO_Saves"
            overlay.mkdir(parents=True)
            (overlay / "local.ess").write_bytes(b"outside MO2")
            print("USVFS fixture: portable profile with a real save in the virtual destination", flush=True)
            run(launcher, windows(search), windows(profile / "saves"), windows(overlay), "253", "virtual")
            print("USVFS fixture: profile-local saves disabled", flush=True)
            (profile / "settings.ini").write_text("[General]\nLocalSaves=false\n")
            run(launcher, windows(search), windows(profile / "saves"), windows(target), "252", "virtual")
            print("USVFS fixture: wrong physical mapping", flush=True)
            (profile / "settings.ini").write_text("[General]\nLocalSaves=true\n")
            other = root / "paired" / "__MO_Saves"
            next(other.glob("*.ess")).write_bytes(b"from the active mapping")
            run(launcher, windows(search), windows(other), windows(target), "252", "redirected")

            print("USVFS fixture: verified portable Skyrim VR profile", flush=True)
            (mo2 / "ModOrganizer.ini").write_text("[General]\ngameName=Skyrim VR\nselected_profile=@ByteArray(Default)\n")
            (profile / "skyrimvr.ini").write_text("[General]\nsLocalSavePath=__MO_Saves\\\n")
            run(launcher, windows(search), windows(profile / "saves"), windows(target), "252", "physical")


if __name__ == "__main__":
    main()
