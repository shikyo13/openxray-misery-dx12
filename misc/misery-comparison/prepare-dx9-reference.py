"""Preserve an independent, hash-checked copy of the user's current DX9 game."""
from pathlib import Path
import hashlib
import json
import shutil
from datetime import datetime, timezone

source = Path(r"D:/SteamLibrary/steamapps/common/Stalker Call of Pripyat")
target = Path(r"D:/Codex/MISERY-DX12/reference/DX9_MAIN_2026-09-08")
record = Path(__file__).parent / "runtime/dx9-main-reference-manifest.json"
assert not target.exists(), "Preserve existing reference; choose a new directory"
assert not record.exists(), "Preserve existing manifest"
target.mkdir(parents=True)
rows = []

def sha(path):
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()

def copy_file(src, relative):
    assert not src.is_symlink(), f"Unexpected link: {src}"
    dest = target / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    before = sha(src)
    shutil.copy2(src, dest)
    assert sha(dest) == before, str(relative)
    rows.append({"path": relative.as_posix(), "source": str(src),
                 "bytes": src.stat().st_size, "sha256": before})

for name in ("bin", "gamedata", "levels", "localization", "mp", "patches", "resources"):
    folder = source / name
    for src in sorted(folder.rglob("*")):
        if src.is_file():
            copy_file(src, src.relative_to(source))
    print(f"Copied and verified {name}; {len(rows)} files so far", flush=True)
copy_file(source / "fsgame.ltx", Path("fsgame.ltx"))
copy_file(source / "_appdata_/user.ltx", Path("_appdata_/user.ltx"))
for suffix in (".scop", ".dds"):
    original = source / ("_appdata_/savedgames/sniper - arrival at the skadovsk" + suffix)
    copy_file(original, Path("_appdata_/savedgames/dx9_reference_skadovsk" + suffix))
record.write_text(json.dumps({
    "at": datetime.now(timezone.utc).isoformat(), "source": str(source),
    "target": str(target), "files": len(rows),
    "bytes": sum(row["bytes"] for row in rows), "entries": rows,
    "notes": "Independent copies; no hardlinks. Original files were only read. Test hooks and a windowed profile will be recorded separately."
}, indent=2), encoding="utf-8")
print(json.dumps({"target": str(target), "manifest": str(record),
                  "files": len(rows), "bytes": sum(row["bytes"] for row in rows)}), flush=True)
