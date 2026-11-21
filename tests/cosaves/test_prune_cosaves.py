import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import time
import unittest
from unittest.mock import patch

from tools import prune_cosaves


class PruneCosaveTests(unittest.TestCase):
    def test_windows_directory_entry_identity_differs_from_direct_stat(self):
        orphan = self.file("orphan.skse")
        with os.scandir(self.folder) as entries:
            entry = next(entries)
            self.assertEqual(entry.name, orphan.name)
        info = orphan.lstat()
        stat_result = SimpleNamespace(st_ino=0, st_dev=0, st_mode=info.st_mode, st_size=info.st_size,
                                      st_mtime_ns=info.st_mtime_ns, st_ctime_ns=info.st_ctime_ns)
        with patch.object(os, "scandir") as scan:
            scan.return_value.__enter__.side_effect = lambda: iter([entry])
            with patch.object(os.DirEntry, "stat", return_value=stat_result):
                planned, moved, recovery = prune_cosaves.prune(self.folder, self.cutoff, True, offline=True)
        self.assertEqual((planned, moved), (1, 1))
        self.assertTrue((recovery / orphan.name).exists())

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name).resolve(strict=True) / "saves"
        self.folder.mkdir()
        self.cutoff = time.time_ns() - 60_000_000_000

    def file(self, name, old=True):
        file = self.folder / name
        file.write_bytes(name.encode())
        if old:
            old_time = self.cutoff - 1_000_000_000
            os.utime(file, ns=(old_time, old_time))
        return file

    def test_dry_run_and_recovery_keep_matching_and_ambiguous_files(self):
        keep = [self.file(name) for name in (
            "Save1.ess", "save1.SKSE", "AutoSave9.ESS", "autosave9.skse", "QuickSave1.ess", "quicksave1.SKSE",
            "AutoSave8.ess.tmp", "AutoSave8.skse", "QuickSave2.tmp", "quicksave2.skse",
            "readme.txt",
        )]
        keep.append(self.file("new.skse", old=False))
        (self.folder / "folder.skse").mkdir()
        if not (self.folder / "SAVE1.ess").exists():
            keep += [self.file("duplicate.skse"), self.file("DUPLICATE.SKSE")]
        link = self.folder / "link.skse"
        try:
            link.symlink_to(keep[0])
            (self.folder / "broken.ess").symlink_to(self.folder / "missing")
        except OSError:
            pass
        else:
            keep += [link, self.file("broken.skse")]
        stale = self.file("OldAutosave.skse")
        self.assertEqual(prune_cosaves.prune(self.folder, self.cutoff), (1, 0, None))
        self.assertTrue(stale.is_file())
        self.assertEqual(list(self.folder.parent.iterdir()), [self.folder])
        with self.assertRaises(ValueError):
            prune_cosaves.prune(self.folder, self.cutoff, apply=True)
        planned, moved, recovery = prune_cosaves.prune(self.folder, self.cutoff, apply=True, offline=True)
        self.assertEqual((planned, moved), (1, 1))
        self.assertEqual(recovery.parent, self.folder.parent)
        self.assertEqual((recovery / stale.name).read_bytes(), stale.name.encode())
        self.assertFalse(stale.exists())
        self.assertTrue(all(file.exists() for file in keep))
        self.assertEqual(prune_cosaves.prune(self.folder, self.cutoff, apply=True, offline=True), (0, 0, None))

    def test_failed_move_reports_recovery_folder_for_files_already_moved(self):
        first = self.file("a.skse")
        second = self.file("b.skse")
        rename = Path.rename

        def fail_second(source, destination):
            if source == second:
                raise PermissionError("file locked")
            return rename(source, destination)

        with patch.object(Path, "rename", fail_second):
            with self.assertRaisesRegex(OSError, "Moved 1 to .*cosave-recovery"):
                prune_cosaves.prune(self.folder, self.cutoff, True, offline=True)
        self.assertFalse(first.exists())
        self.assertTrue(second.is_file())
        recovery, = (path for path in self.folder.parent.iterdir() if path != self.folder)
        self.assertTrue((recovery / first.name).is_file())

    def test_recheck_preserves_new_partner_and_replaced_cosave(self):
        first = self.file("AutoSave1.skse")
        second = self.file("QuickSave1.skse")
        inventory = prune_cosaves.inventory

        def add_partner(folder, cutoff):
            planned = inventory(folder, cutoff)
            if not (folder / "autosave1.ESS").exists():
                self.file("autosave1.ESS")
                original = second.lstat()
                second.rename(self.folder.parent / second.name)
                second.write_bytes(b"x" * len(second.name))
                os.utime(second, ns=(original.st_atime_ns, original.st_mtime_ns))
            return planned

        with patch.object(prune_cosaves, "inventory", side_effect=add_partner):
            self.assertEqual(prune_cosaves.prune(self.folder, self.cutoff, True, offline=True), (2, 0, None))
        self.assertTrue(first.is_file())
        self.assertEqual(second.read_bytes(), b"x" * len(second.name))

    def test_recheck_after_creating_recovery_directory(self):
        stale = self.file("Autosave1.skse")
        mkdtemp = tempfile.mkdtemp

        def add_partner(*args, **kwargs):
            self.file("autosave1.ESS")
            return mkdtemp(*args, **kwargs)

        with patch.object(prune_cosaves.tempfile, "mkdtemp", side_effect=add_partner):
            self.assertEqual(prune_cosaves.prune(self.folder, self.cutoff, True, offline=True), (1, 0, None))
        self.assertTrue(stale.is_file())

    def test_incomplete_scan_leaves_cosaves_in_place(self):
        stale = self.file("stale.skse")
        with patch.object(prune_cosaves, "inventory", side_effect=PermissionError):
            with self.assertRaises(PermissionError):
                prune_cosaves.prune(self.folder, self.cutoff, True, offline=True)
        self.assertTrue(stale.is_file())


if __name__ == "__main__":
    unittest.main()
