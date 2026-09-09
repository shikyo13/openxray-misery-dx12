"""Preserve a retired package's user data and build sources before native PS deletion."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import zipfile


def inventory(root):
    rows = []
    pending = [root]
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                if info.st_file_attributes & 0x400:
                    raise RuntimeError('Refusing a reparse point: ' + entry.path)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(Path(entry.path))
                else:
                    rows.append((Path(entry.path).relative_to(root).as_posix(), info.st_size, info.st_mtime_ns))
    return sorted(rows)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument('--package', type=Path, required=True)
parser.add_argument('--archive-root', type=Path, required=True)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
if args.verify_only and Path(__file__).with_name('retirement.pause').exists():
    print('Retirement paused at a safe boundary before deleting the next preserved package.')
    raise SystemExit(75)
package = args.package.resolve(strict=True)
if not re.fullmatch(r'MISERY_DX12_DEV_\d+\.\d+(?:\.\d+)?', package.name):
    raise SystemExit('Unexpected package name.')
game = package / 'game'
if game.is_symlink() or game.is_junction():
    raise SystemExit('Refusing a linked game directory.')
manifest = json.loads((package / 'manifest.json').read_text(encoding='utf-8-sig'))
rows = inventory(game)
tree_hash = hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest()
archive_dir = args.archive_root.resolve() / package.name
receipt_path = archive_dir / 'receipt.json'
archive_path = archive_dir / 'preserved-game.zip'
if receipt_path.exists():
    receipt = json.loads(receipt_path.read_text())
    if receipt['package'] != str(package) or receipt['inventory_sha256'] != tree_hash:
        raise SystemExit('Package changed since preservation; do not delete it.')
    if digest(archive_path) != receipt['archive_sha256']:
        raise SystemExit('Preservation archive hash mismatch.')
    print(json.dumps(receipt))
    raise SystemExit(0)
if args.verify_only:
    raise SystemExit('No verified preservation receipt.')

known = {r['path']: r for r in manifest['files']}
created = datetime.datetime.fromisoformat(manifest['created'].replace('Z', '+00:00')).timestamp()
source_extensions = {'.ltx', '.ini', '.script', '.lua', '.xml', '.json', '.txt', '.md',
                     '.hlsl', '.h', '.hpp', '.s', '.vs', '.ps', '.gs', '.cs', '.frag', '.vert'}
selected = []
for name, size, modified_ns in rows:
    path = Path(name)
    parts = path.parts
    # Rebuildable shader caches are the only excluded writable-profile data.
    cache = parts[0] == '_appdata_' and any(p.lower() in {'shaders_cache', 'shader_cache', 'shadercache'} for p in parts[1:-1])
    if cache:
        continue
    preserve = (parts[0] in {'_appdata_', 'bin'} or len(parts) == 1 or
                path.suffix.lower() in source_extensions or
                name.startswith(('gamedata/configs/', 'gamedata/scripts/', 'gamedata/shaders/')))
    record = known.get(name)
    if not record or size != record['bytes']:
        preserve = True
    elif not preserve and modified_ns / 1e9 > created:
        # Preserve any asset edited after packaging, not merely text or saves.
        preserve = digest(game / name) != record['sha256']
    if preserve:
        selected.append((name, size, modified_ns))

archive_dir.mkdir(parents=True, exist_ok=True)
partial = archive_dir / 'preserved-game.zip.partial'
if partial.exists() or archive_path.exists():
    raise SystemExit('Unfinished archive exists; preserve and inspect it before retrying.')
selected_bytes = sum(r[1] for r in selected)
if shutil.disk_usage(archive_dir).free < selected_bytes + 2 * 1024**3:
    raise SystemExit('Insufficient archive space; nothing was deleted.')
with zipfile.ZipFile(partial, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as archive:
    for name, _, _ in selected:
        archive.write(game / name, 'game/' + name)
    archive.writestr('preserved-files.json', json.dumps(selected, indent=2))
with zipfile.ZipFile(partial) as archive:
    bad = archive.testzip()
    if bad:
        raise RuntimeError('Archive CRC failed: ' + bad)
    if len(archive.infolist()) != len(selected) + 1:
        raise RuntimeError('Archive file count mismatch.')
partial.rename(archive_path)
receipt = dict(package=str(package), game=str(game), inventory_sha256=tree_hash,
               game_files=len(rows), game_bytes=sum(r[1] for r in rows),
               preserved_files=len(selected), preserved_uncompressed_bytes=selected_bytes,
               archive=str(archive_path), archive_bytes=archive_path.stat().st_size,
               archive_sha256=digest(archive_path), archive_crc_verified=True,
               created=datetime.datetime.now(datetime.timezone.utc).isoformat(),
               preserved='All saves, profiles, screenshots/logs, binaries, scripts, configs, shaders, extra files and detected post-package asset edits; regenerated shader caches excluded.',
               status='Archived and verified; game directory not yet removed')
receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt))
