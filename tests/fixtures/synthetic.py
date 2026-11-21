import sys
from pathlib import Path


def create(root):
    for name, saves, orphans in (("paired", 252, 0), ("orphans", 252, 10507), ("large", 2048, 0),
                                  ("empty", 0, 0), ("cosaves-only", 0, 30)):
        directory = root / name / "__MO_Saves"
        directory.mkdir(parents=True)
        character = "436861726163746572204E616D65205769746820537061636573"
        for i in range(saves):
            prefix = ("Save", "Autosave", "Quicksave")[i % 3]
            stem = f"{prefix}{i}_{character}_WhiterunDragonsreach_052316_20261014111741_124_1"
            (directory / f"{stem}.{'ESS' if i % 2 else 'ess'}").write_bytes(b"fixture")
            (directory / f"{stem}.skse").touch()
        for i in range(orphans):
            (directory / f"Autosave{i % 30}_{character}_WhiterunDragonsreach_052316_{i:014}_124_1.skse").touch()

    redirected = root / "redirected" / "__MO_Saves"
    redirected.mkdir(parents=True)
    (redirected / "real.ess").write_bytes(b"only on disk")
    (redirected / next((root / "orphans" / "__MO_Saves").glob("*.ess")).name).write_bytes(b"shadowed")

    standard = root / "Skyrim Special Edition" / "Saves"
    standard.mkdir(parents=True)
    for filename in ("ABC.ESS", "Café.ess", "日本語.ess", "Save with spaces.ess", "Ä" * 100 + ".ess"):
        (standard / filename).write_bytes(filename.encode())
    (standard / "Directory.ess").mkdir()
    (standard / "ABC.skse").touch()
    (standard / "write.ess.tmp").touch()


if __name__ == "__main__":
    create(Path(sys.argv[1]))
