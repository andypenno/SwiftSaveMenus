import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from tools import package_source


class SourceArchiveTests(unittest.TestCase):
    def test_preserves_git_symlink_on_checkout_without_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
            files = (
                "LICENSE", "NOTICE", "lib/commonlibsse-ng/COPYING",
                "lib/commonlibsse-ng/EXCEPTIONS.md",
                "lib/commonlibsse-ng/extern/openvr/headers/openvr.h",
                "docs/example.txt",
            )
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("file contents\n", encoding="utf-8")
            subprocess.run(["git", "add", "--", *files], cwd=root, check=True)

            link = "docs/link.txt"
            target = b"example.txt"
            (root / link).write_bytes(b"materialized checkout entry")
            oid = subprocess.check_output(["git", "hash-object", "-w", "--stdin"], cwd=root, input=target).decode().strip()
            subprocess.run(["git", "update-index", "--add", "--cacheinfo", f"120000,{oid},{link}"], cwd=root, check=True)

            output = root / "source.zip"
            with patch.object(package_source, "ROOT", root):
                package_source.package_source(output)
            with ZipFile(output) as archive:
                info = archive.getinfo(f"source/{link}")
                self.assertEqual(info.create_system, 3)
                self.assertTrue(stat.S_ISLNK(info.external_attr >> 16))
                self.assertEqual(archive.read(info), target)
