import argparse
import stat
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]


def package_source(output: Path) -> None:
    tracked = subprocess.check_output(["git", "ls-files", "-z", "--stage", "--recurse-submodules"], cwd=ROOT)
    paths = {}
    for entry in filter(None, tracked.split(b"\0")):
        metadata, name = entry.split(b"\t", 1)
        mode, oid, stage = metadata.decode().split()
        if stage != "0":
            raise ValueError("Resolve staged conflicts before packaging source")
        paths[Path(name.decode())] = (mode, oid)
    required = [Path(name) for name in (
        "LICENSE", "NOTICE", "lib/commonlibsse-ng/COPYING", "lib/commonlibsse-ng/EXCEPTIONS.md",
        "lib/commonlibsse-ng/extern/openvr/headers/openvr.h",
    )]
    if not all(path in paths for path in required):
        raise ValueError("Source checkout is missing tracked license files or submodules")
    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED) as archive:
        for path, (mode, oid) in sorted(paths.items()):
            name = f"{output.stem}/{path.as_posix()}"
            if mode == "120000":
                # Git records the link target even when Windows checks it out as a file.
                target = subprocess.check_output(["git", "cat-file", "blob", oid], cwd=(ROOT / path).parent)
                link = ZipInfo(name)
                link.create_system = 3
                link.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive.writestr(link, target)
            else:
                archive.write(ROOT / path, name)
    with ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError("Source archive verification failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    package_source(parser.parse_args().output)
