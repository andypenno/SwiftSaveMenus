import argparse
import ctypes
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile


class SkseHeader(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ("skse", "game", "editor_version", "is_editor")]


def pack(major: int, minor: int, patch: int) -> int:
    return major << 24 | minor << 16 | patch << 4


def check_package(artifact: Path) -> None:
    root = Path(__file__).resolve().parents[2]
    dll_path = "SKSE/Plugins/SwiftSaveMenus.dll"
    ini_path = "SKSE/Plugins/SwiftSaveMenus.ini"
    notices = {
        "SKSE/Plugins/SwiftSaveMenus-LICENSE.txt": root / "LICENSE",
        "SKSE/Plugins/SwiftSaveMenus-NOTICE.txt": root / "NOTICE",
        "SKSE/Plugins/SwiftSaveMenus-CommonLib-exceptions.txt": root / "lib/commonlibsse-ng/EXCEPTIONS.md",
    }
    with ZipFile(artifact) as archive:
        if sorted(archive.namelist()) != sorted((dll_path, ini_path, *notices)) or archive.testzip() is not None:
            raise ValueError("Invalid mod-manager ZIP")
        for name, source in notices.items():
            if archive.read(name) != source.read_bytes():
                raise ValueError(f"Packaged license notice differs from {source.name}")
        if archive.read(ini_path) != (root / "config" / "SwiftSaveMenus.ini").read_bytes():
            raise ValueError("Packaged INI differs from the repository default")
        dll = archive.read(dll_path)

    with TemporaryDirectory(prefix="swiftsavemenus-package-") as directory:
        plugin = Path(directory) / "SwiftSaveMenus.dll"
        plugin.write_bytes(dll)
        library = ctypes.WinDLL(str(plugin))
        try:
            getattr(library, "SKSEPlugin_Query")
            load = library.SKSEPlugin_Load
            load.argtypes = [ctypes.POINTER(SkseHeader)]
            load.restype = ctypes.c_bool
            for header in (
                SkseHeader(pack(2, 0, 12), pack(1, 4, 15), 0, 1),
                SkseHeader(pack(2, 0, 11), pack(1, 4, 15), 0, 0),
                SkseHeader(pack(2, 3, 0), pack(1, 7, 104), 0, 0),
                SkseHeader(pack(2, 3, 1), pack(1, 9, 32), 0, 0),
            ):
                if load(ctypes.byref(header)):
                    raise ValueError("Plugin accepted an unsupported launch")
            ctypes.c_uint8.in_dll(library, "SKSEPlugin_Version")
        finally:
            free_library = ctypes.WinDLL("kernel32", use_last_error=True).FreeLibrary
            free_library.argtypes = (ctypes.c_void_p,)
            free_library.restype = ctypes.c_int
            if not free_library(library._handle):
                raise ctypes.WinError(ctypes.get_last_error())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    check_package(parser.parse_args().artifact)
