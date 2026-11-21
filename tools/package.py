import argparse
import struct
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

PLUGIN_PATH = "SKSE/Plugins/SwiftSaveMenus.dll"
CONFIG_PATH = "SKSE/Plugins/SwiftSaveMenus.ini"
ROOT = Path(__file__).resolve().parents[1]


def package(dll: Path, output: Path) -> None:
    data = dll.read_bytes()
    if len(data) < 64 or not data.startswith(b"MZ"):
        raise ValueError("Expected a Windows DLL")
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if header + 24 > len(data) or data[header:header + 4] != b"PE\0\0":
        raise ValueError("Missing PE header")
    machine = struct.unpack_from("<H", data, header + 4)[0]
    flags = struct.unpack_from("<H", data, header + 22)[0]
    if machine != 0x8664 or not flags & 0x2000:
        raise ValueError("Expected an x64 DLL")

    output.parent.mkdir(parents=True, exist_ok=True)
    config = (ROOT / "config" / "SwiftSaveMenus.ini").read_bytes()
    files = {PLUGIN_PATH: data, CONFIG_PATH: config}
    for name in ("LICENSE", "NOTICE"):
        files[f"SKSE/Plugins/SwiftSaveMenus-{name}.txt"] = (ROOT / name).read_bytes()
    files["SKSE/Plugins/SwiftSaveMenus-CommonLib-exceptions.txt"] = (
        ROOT / "lib/commonlibsse-ng/EXCEPTIONS.md").read_bytes()
    with ZipFile(output, "w", compression=ZIP_DEFLATED) as archive:
        for name, contents in files.items():
            archive.writestr(name, contents)
    with ZipFile(output) as archive:
        if archive.namelist() != list(files) or any(archive.read(name) != data for name, data in files.items()):
            raise ValueError("MO2 archive verification failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    package(args.dll, args.output)
