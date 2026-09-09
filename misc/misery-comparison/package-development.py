"""Create an independent, private development package from the verified runtime."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('--version', required=True)
parser.add_argument('--output-root', type=Path, help='Directory for the independent package; defaults to project outputs.')
args = parser.parse_args()
if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', args.version):
    raise SystemExit('Version must contain two or three numeric components.')

work = Path(__file__).resolve().parent
source = work / 'runtime/misery'
output_root = args.output_root.resolve() if args.output_root else work.parent / 'outputs'
package = output_root / ('MISERY_DX12_DEV_' + args.version)
game = package / 'game'
if package.exists():
    raise SystemExit('Package already exists; do not overwrite a user-played profile.')
if source.joinpath('gamedata/scripts/ui_main_menu.script').read_bytes() != source.joinpath('reference/ui_main_menu.before-dx12-menu-probe.script').read_bytes():
    raise SystemExit('Restore the original menu script before packaging.')
if b'actor_class\t\t\t\t= sniper' not in source.joinpath('gamedata/configs/creatures/actor.ltx').read_bytes():
    raise SystemExit('Restore the customized Sniper class before packaging.')

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def ignore_diagnostics(folder, names):
    return [name for name in names if name.startswith('dx12_') and name.endswith('.script')]

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
(package / 'Launch MISERY DX12.cmd').write_text(
    '@echo off\ncd /d "%~dp0game"\nbin\\xr_3da.exe -fsltx fsgame.ltx -nosplash\n', encoding='ascii')

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
}
(package / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in manifest.items() if k != 'files'}, indent=2), flush=True)
