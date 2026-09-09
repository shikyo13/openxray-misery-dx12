"""Create an independent, private development package from the verified runtime."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import argparse
import re
import os

parser = argparse.ArgumentParser()
parser.add_argument('--version', required=True)
parser.add_argument('--output-root', type=Path, help='Directory for the independent package; defaults to project outputs.')
parser.add_argument('--batch-shadow-copies', action='store_true', help='Enable the tested grouped shadow-copy path in the normal launcher.')
parser.add_argument('--build-identity', type=Path, help='Archived executable/PDB and compiled source identity to preserve with this package.')
parser.add_argument('--check-storage', action='store_true', help='Check retention and free-space limits without creating or copying anything.')
args = parser.parse_args()
if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', args.version):
    raise SystemExit('Version must contain two or three numeric components.')

work = Path(__file__).resolve().parent
source = work / 'runtime/misery'
policy = json.loads((work / 'storage-policy.json').read_text(encoding='utf-8-sig'))
output_root = args.output_root.resolve() if args.output_root else Path(policy['package_output_root']).resolve()
final_package = output_root / ('MISERY_DX12_DEV_' + args.version)
package = output_root / (final_package.name + '.incomplete')
game = package / 'game'

def tree_bytes(root):
    if not root.exists():
        return 0
    total = 0
    pending = [root]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                if info.st_file_attributes & 0x400:
                    raise SystemExit('Storage check refuses reparse points: ' + entry.path)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(entry.path)
                else:
                    total += info.st_size
    return total

def existing_parent(path):
    while not path.exists():
        path = path.parent
    return path

# Check before creating a directory or copying the approximately 11 GiB payload.
roots = {work.parent / 'outputs', Path(policy['package_output_root']).resolve(), output_root}
full_packages = sorted(str(p) for root in roots if root.exists()
                       for p in root.glob('MISERY_DX12_DEV_*') if (p / 'game').is_dir())
estimated_bytes = sum(tree_bytes(source / folder) for folder in
                      ('resources', 'levels', 'localization', 'patches', 'mp', 'gamedata', 'bin'))
estimated_bytes += tree_bytes(work / 'misery-compat') + 64 * 1024**2
destination_free = shutil.disk_usage(existing_parent(output_root)).free
system_free = shutil.disk_usage(os.environ.get('SystemDrive', 'C:') + '/').free
retired_bytes = tree_bytes(Path(policy['retired_archive_root']))
errors = []
if len(full_packages) >= policy['max_full_packages']:
    errors.append('Retire the older rollback with retire-development-packages.ps1 before making another full package; maximum is ' + str(policy['max_full_packages']) + '.')
if destination_free - estimated_bytes < policy['minimum_free_gib_after_packaging'] * 1024**3:
    errors.append('Packaging would breach the destination free-space reserve.')
if system_free < policy['minimum_system_free_gib'] * 1024**3:
    errors.append('System-drive free space is below the required reserve.')
if retired_bytes > policy['max_retired_archive_gib'] * 1024**3:
    errors.append('Retired archives exceed their storage budget; consolidate old build binaries while preserving saves before another release.')
storage_check = dict(full_packages=full_packages, estimated_package_bytes=estimated_bytes,
                     destination_free_bytes=destination_free, system_free_bytes=system_free,
                     retired_archive_bytes=retired_bytes,
                     maximum_full_packages=policy['max_full_packages'], errors=errors)
if args.check_storage or errors:
    print(json.dumps(storage_check, indent=2), flush=True)
if errors:
    raise SystemExit(1)
if args.check_storage:
    raise SystemExit(0)
if not args.build_identity:
    raise SystemExit('Full packages require --build-identity for an archived, committed, tested build.')
for repository in (work / 'engine', work / 'misery-compat'):
    if subprocess.check_output(['git', '-C', str(repository), 'status', '--porcelain'], text=True).strip():
        raise SystemExit('Commit or preserve pending changes before packaging: ' + str(repository))
if final_package.exists() or package.exists():
    raise SystemExit('Package or incomplete staging already exists; preserve it before retrying.')
if source.joinpath('gamedata/scripts/ui_main_menu.script').read_bytes() != source.joinpath('reference/ui_main_menu.before-dx12-menu-probe.script').read_bytes():
    raise SystemExit('Restore the original menu script before packaging.')
if b'actor_class\t\t\t\t= sniper' not in source.joinpath('gamedata/configs/creatures/actor.ltx').read_bytes():
    raise SystemExit('Restore the customized Sniper class before packaging.')

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def ignore_diagnostics(folder, names):
    return [name for name in names if name.startswith('dx12_') and name.endswith('.script')]

build_identity = None
if args.build_identity:
    build_identity = json.loads(args.build_identity.read_text(encoding='utf-8-sig'))
    if not build_identity.get('committed_source'):
        raise SystemExit('Build identity has no committed source revision.')
    subprocess.run(['git', '-C', str(work / 'engine'), 'cat-file', '-e',
                    build_identity['committed_source'] + '^{commit}'], check=True)
    for name, key in (('xr_3da.exe', 'exe_sha256'), ('xr_3da.pdb', 'pdb_sha256')):
        if sha(source / 'bin' / name) != build_identity[key]:
            raise SystemExit('Staged binary differs from the specified build identity: ' + name)
    patch = args.build_identity.parent / 'source.patch'
    if sha(patch) != build_identity['source_patch_sha256']:
        raise SystemExit('Archived source patch differs from the specified build identity.')

game.mkdir(parents=True)
for folder in ('resources', 'levels', 'localization', 'patches', 'mp', 'gamedata', 'bin'):
    shutil.copytree(source / folder, game / folder, ignore=ignore_diagnostics)
    print('Copied ' + folder, flush=True)
for name in ('fsgame.ltx', 'misery_options.ini'):
    shutil.copy2(source / name, game / name)
(game / '_appdata_/savedgames').mkdir(parents=True)
shutil.copy2(source / '_appdata_/user.ltx', game / '_appdata_/user.ltx')
# Include only class-matching development checkpoints, never original saves.
for checkpoint in ('dx12_normal_sniper', 'dx12_skadovsk_npc'):
    for ext in ('.scop', '.dds'):
        name = checkpoint + ext
        shutil.copy2(source / '_appdata_/savedgames' / name, game / '_appdata_/savedgames' / name)
for scene in ('interior', 'outdoor', 'rain', 'night'):
    name = 'graphics_comparison_' + scene + '.xrdemo'
    shutil.copy2(source / '_appdata_/savedgames' / name, game / '_appdata_/savedgames' / name)
shutil.copytree(work / 'misery-compat', package / 'compatibility-overlay', ignore=shutil.ignore_patterns('.git'))

# Windows double-click launcher; all writable paths remain in this package.
launch_arguments = '-fsltx fsgame.ltx -nosplash'
if args.batch_shadow_copies:
    launch_arguments += ' -local_shadow_batch_copies'
(package / 'Launch MISERY DX12.cmd').write_text(
    '@echo off\ncd /d "%~dp0game"\nbin\\xr_3da.exe ' + launch_arguments + '\n', encoding='ascii')
if build_identity:
    shutil.copy2(args.build_identity, package / 'build-identity.json')
    shutil.copy2(patch, package / 'source.patch')

records = []
for path in sorted(game.rglob('*')):
    if not path.is_file():
        continue
    relative = path.relative_to(game)
    origin = source / relative
    digest = sha(path)
    if sha(origin) != digest:
        raise RuntimeError('Package copy differs: ' + str(relative))
    records.append({'path': relative.as_posix(), 'bytes': path.stat().st_size, 'sha256': digest})
    if len(records) % 4000 == 0:
        print('Hash-verified ' + str(len(records)) + ' files', flush=True)

engine = work / 'engine'
manifest = {
    'created': datetime.now(timezone.utc).isoformat(),
    'status': 'Development build; complete gameplay and extended stability are not yet verified',
    'source_commit': subprocess.check_output(['git','-C',str(engine),'rev-parse','HEAD'], text=True).strip(),
    'source_status': subprocess.check_output(['git','-C',str(engine),'status','--porcelain'], text=True).strip(),
    'exe_sha256': sha(game / 'bin/xr_3da.exe'),
    'overlay_version': json.loads((work / 'misery-compat/patches.json').read_text())['version'],
    'overlay_commit': subprocess.check_output(['git','-C',str(work / 'misery-compat'),'rev-parse','HEAD'], text=True).strip(),
    'overlay_status': subprocess.check_output(['git','-C',str(work / 'misery-compat'),'status','--porcelain'], text=True).strip(),
    'active_class': 'sniper; exact customized class files restored after tests',
    'files': records,
    'copied_bytes': sum(record['bytes'] for record in records),
    'copy_verification': 'Every packaged game file matches the independently staged runtime by SHA256',
    'reference_install': 'Preserved; no original save imported, converted or modified',
    'launch_arguments': launch_arguments,
    'batch_shadow_copies_in_normal_launcher': args.batch_shadow_copies,
}
if build_identity:
    manifest['packaging_source_commit'] = manifest['source_commit']
    manifest['source_commit'] = build_identity['committed_source']
    manifest['build_identity'] = build_identity
    manifest['pdb_sha256'] = build_identity['pdb_sha256']
(package / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
if package.parent != output_root or final_package.parent != output_root:
    raise RuntimeError('Package publication escaped its output directory.')
package.rename(final_package)
print('Published verified package: ' + str(final_package), flush=True)
print(json.dumps({k:v for k,v in manifest.items() if k != 'files'}, indent=2), flush=True)
