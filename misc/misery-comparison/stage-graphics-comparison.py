from pathlib import Path
import hashlib
import json
import math
import shutil
import struct

work = Path(__file__).parent
dx9 = Path(r"D:/Codex/MISERY-DX12/reference/DX9_MAIN_2026-09-08")
dx12 = work / "runtime/misery"
record = work / "runtime/graphics-comparison-staging.json"
assert not record.exists()
rows = []
for root in (dx9, dx12):
    script = root / "gamedata/scripts/graphics_comparison_baseline.script"
    assert not script.exists()
    shutil.copy2(work / script.name, script)
    for name, position, target in (
        ("interior", (128.80002, -5.74269, 177.79997), (123.80002, -6.14269, 177.79997)),
        ("outdoor", (164.53406, -3.95935, 152.64307), (158.53406, -5.25935, 159.64307)),
        ("rain", (164.53406, -3.95935, 152.64307), (158.53406, -5.25935, 159.64307)),
        ("night", (164.53406, -3.95935, 152.64307), (158.53406, -5.25935, 159.64307)),
    ):
        def unit(v):
            scale = math.sqrt(sum(x*x for x in v))
            return tuple(x/scale for x in v)
        def cross(a, b):
            return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
        forward = unit(tuple(b-a for a,b in zip(position, target)))
        right = unit(cross((0,1,0), forward))
        up = cross(forward, right)
        # DirectX row-vector view matrix, consumed unchanged by CDemoPlay in both engines.
        values = [right[0],up[0],forward[0],0, right[1],up[1],forward[1],0,
                  right[2],up[2],forward[2],0,
                  -sum(a*b for a,b in zip(position,right)),
                  -sum(a*b for a,b in zip(position,up)),
                  -sum(a*b for a,b in zip(position,forward)),1]
        camera = root / ("_appdata_/savedgames/graphics_comparison_" + name + ".xrdemo")
        assert not camera.exists()
        camera.write_bytes(struct.pack("<16f", *values)*120)
        rows.append({"path": str(camera), "sha256": hashlib.sha256(camera.read_bytes()).hexdigest(),
                     "position": position, "target": target})
    profile = root / "_appdata_/user.ltx"
    backup = work / ("runtime/graphics-comparison-" + ("dx9" if root == dx9 else "dx12") + "-initial.ltx")
    assert not backup.exists()
    shutil.copy2(profile, backup)
    text = profile.read_text(encoding="utf-8-sig")
    # Match windowed 3440x1440, retain each renderer's actual quality settings.
    # Disable AA in the common comparison; the original profile has r2_aa off.
    settings = {"rs_fullscreen": "off", "rs_v_sync": "off", "vid_mode": "3440x1440"}
    if root == dx12:
        settings.update({"r_aa": "off", "r_fsr_fg": "0", "r_material_mode": "1"})
    lines = [line for line in text.splitlines() if line.split(" ",1)[0] not in settings]
    lines += [key + " " + value for key,value in settings.items()]
    profile.write_text("\n".join(lines)+"\n", encoding="utf-8")
    rows.append({"path": str(profile), "backup": str(backup), "overrides": settings})
binder = dx9 / "gamedata/scripts/bind_stalker.script"
original = binder.read_bytes()
(work / "runtime/graphics-comparison-dx9-binder-original.script").write_bytes(original)
binder.write_bytes(original + b'\r\n-- Isolated comparison observer; retain all normal MISERY updates.\r\nlocal comparison_original_actor_update = actor_binder.update\r\nfunction actor_binder:update(delta)\r\n    comparison_original_actor_update(self, delta)\r\n    graphics_comparison_baseline.update()\r\nend\r\n')
record.write_text(json.dumps(rows, indent=2), encoding="utf-8")
print(record)
