"""Summarize the two recorded game runs; never launch or alter a game."""
from pathlib import Path
import argparse
import csv
import html
import json
import math
import statistics

parser = argparse.ArgumentParser()
parser.add_argument("dx9")
parser.add_argument("dx12")
parser.add_argument("--name", default="MISERY_DX9_DX12_COMPARISON_041")
parser.add_argument("--dx12-label", default="DX12 build 0.40")
parser.add_argument("--previous-report")
parser.add_argument("--settings-audit", help="Source-reviewed settings notes tied to these exact captured runs")
args = parser.parse_args()
dx12_label = html.escape(args.dx12_label)
outputs = Path(__file__).resolve().parent.parent / "outputs"
cases = ("interior", "outdoor", "rain", "night")

def read_profile(path):
    settings = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) == 2 and not fields[0].startswith(";"):
            settings[fields[0]] = fields[1]
    return settings

def percentile(values, fraction):
    ordered = sorted(values)
    point = (len(ordered)-1)*fraction
    low = int(point)
    return ordered[low] + (ordered[min(low+1,len(ordered)-1)]-ordered[low])*(point-low)

def read_run(name):
    root = outputs / "implementation-evidence" / name
    result = json.loads((root/"result.json").read_text(encoding="utf-8-sig"))
    if result["exit_code"] != 0 or not result["exited_before_deadline"]:
        raise ValueError(f"{name}: a failed run cannot be reported as a completed benchmark")
    events = json.loads((root/"observed-events.json").read_text(encoding="utf-8-sig"))
    if not any(e["line"].split("\t")[1] == "complete" for e in events):
        raise ValueError(f"{name}: missing completion marker")
    records = {}
    current = None
    seen = set()
    for event in events:
        # Retail flushes can rewrite the whole log; keep the first observation.
        if event["line"] in seen:
            continue
        seen.add(event["line"])
        fields = event["line"].split("\t")
        if fields[1] == "begin":
            current = fields[2]
            records[current] = {"begin_qpc":event["qpc"]}
        elif fields[1] == "end":
            records[fields[2]]["end_qpc"] = event["qpc"]
            current = None
        elif current and fields[1] in ("pos","dir","lens","clock"):
            records[current][fields[1]] = fields[2:]
    if tuple(records) != cases:
        raise ValueError(f"{name}: missing or reordered scene markers")
    shots = sorted(root.glob("*.jpg"), key=lambda p:p.stat().st_mtime_ns)
    if len(shots) != len(cases):
        raise ValueError(f"{name}: expected exactly four captures, found {len(shots)}")
    with (root/"presents.csv").open(newline="",encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    frequency = result["qpc_frequency"]
    for scene, shot in zip(cases,shots):
        data = records[scene]
        # Exclude logging/capture boundaries by half a second, including any
        # present interval whose start precedes the selected time window.
        first = data["begin_qpc"] + frequency*0.5
        last = data["end_qpc"] - frequency*0.5
        selected = [r for r in rows if first <= int(r["QPCTime"]) < last
                    and int(r["QPCTime"]) - float(r["msBetweenPresents"])*frequency/1000 >= first
                    and float(r["msBetweenPresents"]) > 0]
        times = [float(r["msBetweenPresents"]) for r in selected]
        if len(times) < 100:
            raise ValueError(f"{name}/{scene}: insufficient captured frames")
        mean = statistics.fmean(times)
        slowest = sorted(times,reverse=True)[:max(1,math.ceil(len(times)*0.01))]
        data.update(frames=len(times),seconds=sum(times)/1000,mean_ms=mean,
                    average_fps=1000/mean,p95_ms=percentile(times,.95),
                    p99_ms=percentile(times,.99),one_percent_low_fps=1000/statistics.fmean(slowest),
                    max_ms=max(times),dropped=sum(int(r["Dropped"]) for r in selected),
                    present_modes=sorted({r["PresentMode"] for r in selected}),
                    screenshot=shot.relative_to(outputs).as_posix())
    return {"name":name,"process":result,"scenes":records,
            "profiles": {stage:read_profile(root/f"profile-{stage}.ltx") for stage in ("initial","after")},
            "profile_files": {stage:(root/f"profile-{stage}.ltx").relative_to(outputs).as_posix() for stage in ("initial","after")}}

runs = {"DX9":read_run(args.dx9),"DX12":read_run(args.dx12)}
for scene in cases:
    a,b=(runs[k]["scenes"][scene] for k in ("DX9","DX12"))
    for field in ("pos","dir","lens"):
        av=[float(x) for s in a[field] for x in s.split(",")]
        bv=[float(x) for s in b[field] for x in s.split(",")]
        if len(av)!=len(bv) or any(abs(x-y)>.001 for x,y in zip(av,bv)):
            raise ValueError(f"{scene}: {field} does not match: {a[field]} / {b[field]}")
    def minutes(clock):
        hour,minute=map(int,clock[0].split(":"))
        return hour*60+minute
    if a["clock"][1] != b["clock"][1] or abs(minutes(a["clock"])-minutes(b["clock"]))>1:
        raise ValueError(f"{scene}: time/weather differs materially: {a['clock']} / {b['clock']}")

summary={"runs":runs,"method":"One sequential run per renderer, four fixed cameras, 30-second windows trimmed by 0.5 seconds at both ends. Application present intervals from PresentMon; no frame generation. Save/simulation states differ; recorded game clocks differ by up to one minute. Screenshots are SDR before any driver RTX HDR conversion."}
profile_note = ""
if "-graphics_trace" in runs["DX12"]["process"]["args"]:
    profile_note = "GPU profiling was enabled in this DX12 run; its overhead is included. These results should not be treated as an isolated measurement of the code change."
summary["profiling_note"] = profile_note
summary["dx12_label"] = args.dx12_label
settings_audit = None
if args.settings_audit:
    audit_path = outputs / args.settings_audit
    settings_audit = json.loads(audit_path.read_text(encoding="utf-8-sig"))
    for renderer in ("DX9", "DX12"):
        expected = settings_audit["runs"][renderer]
        actual = runs[renderer]
        if expected["name"] != actual["name"] or expected["exe_sha256"].lower() != actual["process"]["sha256"].lower():
            raise ValueError(f"{renderer}: settings audit belongs to another run/build")
    summary["settings_audit"] = settings_audit
(outputs/(args.name+".json")).write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
sections=[]
table=[]
for scene in cases:
    a,b=(runs[k]["scenes"][scene] for k in ("DX9","DX12"))
    table.append(f"<tr><th>{scene.title()}</th><td>{a['average_fps']:.1f}</td><td>{b['average_fps']:.1f}</td><td>{a['p99_ms']:.1f}</td><td>{b['p99_ms']:.1f}</td></tr>")
    sections.append(f'''<section><h2>{scene.title()}</h2><div class="comparison">
      <img src="{html.escape(b['screenshot'])}" alt="DX12 {scene}">
      <img class="before" src="{html.escape(a['screenshot'])}" alt="DX9 {scene}">
      <span class="label left">DX9 · main installation copy</span><span class="label right">{dx12_label}</span>
      <div class="divider"></div></div>
      <label class="slider">Slide to compare <input type="range" min="0" max="100" value="50" aria-label="{scene} comparison" oninput="this.closest('section').style.setProperty('--split',this.value+'%')"></label>
      <p><a href="{html.escape(a['screenshot'])}">Open full-resolution DX9</a> · <a href="{html.escape(b['screenshot'])}">Open full-resolution DX12</a></p>
      <p class="small">DX9: {a['frames']:,} presents / {a['seconds']:.2f}s, {a['mean_ms']:.2f} ms mean. DX12: {b['frames']:,} presents / {b['seconds']:.2f}s, {b['mean_ms']:.2f} ms mean.</p></section>''')
page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MISERY DX9 / DX12 comparison</title>
<style>body{background:#141714;color:#e5e8dd;font:16px/1.5 system-ui;margin:0;padding:32px;max-width:1700px;margin:auto}h1{font-size:32px}h2{font-size:23px}p{max-width:1000px}a{color:#c7d3a2}table{border-collapse:collapse;margin:24px 0}th,td{text-align:left;padding:10px 22px;border-bottom:1px solid #465043}section{--split:50%;margin:46px 0}.comparison{position:relative;width:100%;line-height:0;background:#000}.comparison img{width:100%;display:block}.before{position:absolute;inset:0;clip-path:inset(0 calc(100% - var(--split)) 0 0)}.divider{position:absolute;top:0;bottom:0;left:var(--split);width:2px;background:#dedf91}.label{position:absolute;top:12px;line-height:1.4;background:#101510d9;padding:7px 12px}.left{left:12px}.right{right:12px}.slider{display:flex;gap:18px;align-items:center;padding:14px 0}.slider input{flex:1}.small{font-size:14px;color:#b1b9aa}.note{border-left:3px solid #b8c391;padding-left:18px}</style>
<h1>MISERY: main DX9 build vs DX12</h1>
<p>3440 × 1440, matched camera positions, directions, 75° field of view and named weather. Each run uses the same resolution and authored brightness, contrast, gamma and tone-map settings. Antialiasing and frame generation are off for the comparison.</p>
<p class="note">These are scene comparisons and short performance samples. The two engines require different save formats, so NPCs and ongoing simulation are not identical. Recorded game clocks differ by up to one minute. Renderer effects also differ. This is not a complete campaign or stability test. Images show the engine's SDR output; RTX HDR is applied later by the driver.</p>
<table><thead><tr><th>Scene</th><th>DX9 avg FPS</th><th>DX12 avg FPS</th><th>DX9 p99 ms</th><th>DX12 p99 ms</th></tr></thead><tbody>'''+''.join(table)+'''</tbody></table><p class="small">FPS = 1000 / mean application present interval. p99 is the 99th-percentile interval; lower is better. Measurement excludes screenshot and transition boundaries. One run per renderer, not a sustained performance claim.</p>'''+''.join(sections)+f'<p><a href="{args.name}.json">Raw measurements and run identities</a></p></html>'
if profile_note:
    page=page.replace("<table>","<p class='small'>"+html.escape(profile_note)+"</p><table>",1)
if args.previous_report:
    page=page.replace("<table>","<p><a href='"+html.escape(args.previous_report,quote=True)+"'>Previous DX12 comparison</a></p><table>",1)

# Keep captured values distinct from source-verified implementation: inherited
# console flags can be accepted/saved while the native renderer never uses them.
settings_html = '<section id="graphics-settings"><h2>Graphics and display settings</h2>'
settings_html += '<p class="note">This compares the builds as configured, including their effects and presentation paths. It does not isolate graphics API efficiency. An equal saved setting does not establish an equal implementation or cost.</p>'
if settings_audit:
    settings_html += '<p>'+html.escape(settings_audit['conclusion'])+'</p>'
    settings_html += '<div class="table-scroll"><table class="settings"><thead><tr><th>Feature</th><th>DX9 reference</th><th>'+dx12_label+'</th><th>What the evidence establishes</th></tr></thead><tbody>'
    for row in settings_audit['features']:
        settings_html += '<tr>'+''.join('<'+tag+'>'+html.escape(row[key])+'</'+tag+'>' for key,tag in [('feature','th'),('DX9','td'),('DX12','td'),('assessment','td')])+'</tr>'
    settings_html += '</tbody></table></div>'
    settings_html += '<p class="small">'+html.escape(settings_audit['limitations'])+'</p>'
    settings_html += '<p><a href="'+html.escape(args.settings_audit,quote=True)+'">Settings audit, source references and measured DX12 effect costs</a></p>'
settings_html += '<details><summary>Captured configuration values and presentation evidence</summary>'
settings_html += '<p class="small">Values below were saved after each run; any startup-to-exit change is shown. They are evidence of configuration, not proof that every control is implemented. “Not stored” does not mean disabled.</p>'
keys = ['renderer','vid_mode','vid_window_mode','rs_fullscreen','rs_v_sync','rs_fps_limit',
        'r_aa','r2_aa','r3_msaa','r_fsr_fg','r__tf_aniso','texture_lod','r__detail_density',
        'r__geometry_lod','rs_vis_distance','r2_sun_details','r_detail_shadows',
        'r_detail_shadow_distance','r_sun_shadows','r2_smap_size','r2_sun_far',
        'r_local_shadows','r_local_shadow_resolution','r2_ssao','r2_ssao_mode',
        'r_ssgi','r2_gi','r_rt_gi','r_path_tracer','r2_sun_shafts','r2_dof_enable',
        'r2_steep_parallax','r4_enable_tessellation','rs_c_brightness','rs_c_contrast',
        'rs_c_gamma','r2_tonemap','r2_tonemap_adaptation','r2_tonemap_middlegray',
        'r2_tonemap_amount','r2_tonemap_lowlum','r2_gloss_factor']
settings_html += '<div class="table-scroll"><table><thead><tr><th>Saved control</th><th>DX9</th><th>DX12</th></tr></thead><tbody>'
for key in keys:
    values = []
    for renderer in ('DX9','DX12'):
        profiles = runs[renderer]['profiles']
        initial = profiles['initial'].get(key,'Not stored')
        after = profiles['after'].get(key,'Not stored')
        values.append(after if initial == after else initial+' → '+after)
    settings_html += '<tr><th>'+html.escape(key)+'</th>'+''.join('<td>'+html.escape(value)+'</td>' for value in values)+'</tr>'
settings_html += '</tbody></table></div>'
for renderer in ('DX9','DX12'):
    modes = sorted({mode for scene in runs[renderer]['scenes'].values() for mode in scene['present_modes']})
    settings_html += '<p>'+renderer+' observed PresentMon modes: '+html.escape(', '.join(modes))+'. '
    settings_html += ' · '.join('<a href="'+html.escape(path,quote=True)+'">'+html.escape(stage)+' profile</a>' for stage,path in runs[renderer]['profile_files'].items())+'</p>'
settings_html += '</details></section>'
page = page.replace('<section><h2>Interior</h2>',settings_html+'<section><h2>Interior</h2>',1)
page = page.replace('</style>', '.table-scroll{overflow-x:auto}.settings{width:100%;font-size:14px}.settings th,.settings td{vertical-align:top;padding:10px 14px}.settings th:first-child{width:15%}.settings td{width:28%}summary{cursor:pointer;color:#c7d3a2}details{border:1px solid #465043;padding:16px} </style>',1)
(outputs/(args.name+".html")).write_text(page,encoding="utf-8")
print(json.dumps({k:{c:{m:r['scenes'][c][m] for m in ('frames','average_fps','mean_ms','p99_ms')} for c in cases} for k,r in runs.items()},indent=2))
