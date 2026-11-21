import hashlib
import json
import os
from pathlib import Path
import shutil
import time


def prepare(source, saves):
    manifest = json.loads((source / "manifest.json").read_text())
    old = time.time() - 48 * 60 * 60
    saves.mkdir(parents=True)
    for item in manifest:
        relative = Path(item["name"])
        if relative.is_absolute() or len(relative.parts) != 2 or ".." in relative.parts:
            raise ValueError("Unexpected Deck fixture path")
        original = source / relative
        target = saves / relative.name
        if target.exists() or hashlib.sha256(original.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError("Deck fixture is duplicated or changed")
        shutil.copyfile(original, target)
        os.utime(target, (old, old))
    return manifest


def verify_recovery(saves, manifest):
    recovery = saves.parent / "SaveLoadFix-cosave-recovery"
    for item in manifest:
        relative = Path(item["name"])
        destination, other = (recovery, saves) if relative.parts[0] == "orphan" else (saves, recovery)
        if (other / relative.name).exists() or hashlib.sha256((destination / relative.name).read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError("Unexpected Deck fixture cleanup result")
