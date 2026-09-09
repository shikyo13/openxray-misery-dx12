"""Analyze recorded priority-1 pairs; never launch or alter a game.

Present intervals use the same 0.5-second trim and interval-start exclusion as
report-graphics-comparison.py. These captures deliberately omit display tracking.
"""
from pathlib import Path
import collections
import argparse
import csv
import hashlib
import json
import math
import re
import statistics

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / 'outputs/implementation-evidence'
CASES = ('interior', 'outdoor', 'rain', 'night')


def stats(values):
    values = sorted(values)
    if not values:
        return {'n': 0}
    def percentile(fraction):
        point = (len(values) - 1) * fraction
        lower = int(point)
        return values[lower] + (values[min(lower + 1, len(values) - 1)] - values[lower]) * (point - lower)
    return dict(n=len(values), mean=statistics.fmean(values), p95=percentile(.95),
                p99=percentile(.99), maximum=values[-1])


def read_run(name, require_trace=True):
    root = EVIDENCE / name
    result = json.loads((root / 'result.json').read_text(encoding='utf-8-sig'))
    assert result['exit_code'] == result['presentmon_exit_code'] == 0
    assert result['exited_before_deadline'] and not result['presentmon_display_tracking']
    events = json.loads((root / 'observed-events.json').read_text(encoding='utf-8-sig'))
    assert any('\tcomplete' in event['line'] for event in events)
    scenes = {}
    active = None
    for event in events:
        fields = event['line'].split('\t')
        if fields[1] == 'begin':
            active = fields[2]
            scenes[active] = {'begin_qpc': event['qpc'], 'begin_time_ms': int(fields[0])}
        elif fields[1] == 'end':
            scenes[fields[2]].update(end_qpc=event['qpc'], end_time_ms=int(fields[0]))
            active = None
        elif active and fields[1] in ('pos', 'dir', 'lens', 'clock'):
            scenes[active][fields[1]] = fields[2:]
    assert tuple(scenes) == CASES
    shots = sorted(root.glob('*.jpg'), key=lambda p: p.stat().st_mtime_ns)
    assert len(shots) == len(CASES)
    with (root / 'presents.csv').open(encoding='utf-8-sig', newline='') as stream:
        presents = list(csv.DictReader(stream))
    timings = []
    if require_trace:
        with (root / 'dx12_graphics.csv').open(encoding='utf-8-sig', newline='') as stream:
            timings = list(csv.DictReader(stream))
    else:
        assert all(flag not in result['args'] for flag in ('-cpu_trace', '-graphics_trace', '-frame_trace'))
        assert not (root / 'dx12_graphics.csv').exists()
    log = (root / 'engine.log').read_text(encoding='utf-8', errors='replace')
    telemetry = [json.loads(line) for line in (root / 'gpu-engine-samples.jsonl').read_text().splitlines()]
    frequency = result['qpc_frequency']
    for scene, shot in zip(scenes.values(), shots):
        first, last = scene['begin_qpc'] + frequency * .5, scene['end_qpc'] - frequency * .5
        start_ms, end_ms = scene['begin_time_ms'] + 500, scene['end_time_ms'] - 500
        selected = [r for r in presents if first <= int(r['QPCTime']) < last
                    and int(r['QPCTime']) - float(r['msBetweenPresents']) * frequency / 1000 >= first
                    and float(r['msBetweenPresents']) > 0]
        assert len(selected) >= 100
        assert {int(r['ProcessID']) for r in selected} == {result['pid']}
        assert len({r['SwapChainAddress'] for r in selected}) == 1
        intervals = [float(r['msBetweenPresents']) for r in selected]
        scene['application_present_ms'] = stats(intervals)
        scene['application_fps'] = 1000 / statistics.fmean(intervals)
        scene['one_percent_low_application_fps'] = 1000 / statistics.fmean(sorted(intervals, reverse=True)[:math.ceil(len(intervals) * .01)])
        scene['present_mode'] = None
        scene['display_latency_and_drops'] = None
        scene['screenshot'] = shot.relative_to(ROOT / 'outputs').as_posix()
        buckets = collections.defaultdict(list)
        quality = set()
        for row in timings:
            if start_ms <= int(row['time_global_ms']) < end_ms:
                buckets[(row['kind'], row['label'])].append(float(row['value_ms']))
                quality.add((int(row['aa']), int(row['ao'])))
        if require_trace:
            assert quality == {(1, 3)}, quality  # actual traced FXAA / AO-high IDs
        scene['trace_quality_aa_ao'] = sorted(quality) if require_trace else None
        scene['timings_ms'] = {kind: {label: stats(values) for (k, label), values in buckets.items() if k == kind}
                               for kind in ('cpu', 'cpu_stage', 'cpu_pass', 'gpu')}
        parallel = [m for m in re.finditer(r'\[ParallelRecord\] frame=(\d+) time=(\d+) jobs=(\d+) threads=(\d+),(\d+) overlap_ms=([\d.]+)', log)
                    if start_ms <= int(m[2]) < end_ms]
        scene['parallel'] = dict(samples=len(parallel), distinct_threads=sum(m[4] != m[5] for m in parallel),
                                 overlap_ms=stats([float(m[6]) for m in parallel]))
        costs = [m for m in re.finditer(r'\[ParallelCosts\] frame=(\d+) time=(\d+) prepare_ms=(\S+) launch_ms=(\S+) wait_ms=(\S+) append_ms=(\S+) restore_ms=(\S+) total_ms=(\S+)', log)
                 if start_ms <= int(m[2]) < end_ms]
        scene['parallel']['costs_ms'] = {label: stats([float(m[index]) for m in costs])
                                        for index, label in enumerate(('prepare', 'launch', 'wait', 'append', 'restore', 'total'), 3)}
        sampled = [x for x in telemetry if first <= x['qpc'] < last]
        background = [v for x in sampled for v in x['engines'] if v['pid'] != result['pid']]
        game_3d_raw = [v['utilization_percent'] for x in sampled for v in x['engines']
                       if v['pid'] == result['pid'] and 'engtype_3d' in v['instance'].lower()]
        game_3d = [v for v in game_3d_raw if math.isfinite(v) and 0 <= v <= 100]
        background_valid = [v for v in background if math.isfinite(v['utilization_percent']) and 0 <= v['utilization_percent'] <= 100]
        scene['gpu_samples'] = dict(count=len(sampled), first_qpc=sampled[0]['qpc'] if sampled else None,
            last_qpc=sampled[-1]['qpc'] if sampled else None, game_3d_percent=stats(game_3d),
            nvidia_overall_percent=stats([float(x['nvidia_gpu_memory_power'][0].split(',')[0]) for x in sampled]),
            background_max_single_engine_percent=max((v['utilization_percent'] for v in background_valid), default=0),
            background_instances=sorted({v['instance'] for v in background_valid}),
            out_of_range_game_counter_values=[v for v in game_3d_raw if not math.isfinite(v) or not 0 <= v <= 100],
            out_of_range_background_counter_count=len(background)-len(background_valid),
            invalid_counter_count=sum(x['invalid_counter_count'] for x in sampled) if all('invalid_counter_count' in x for x in sampled) else None)
        slow = [m for m in re.finditer(r'\[FrameTrace\] frame=(\d+) time=(\d+) total_ms=(\S+) move_ms=(\S+) camera_ms=(\S+) render_ms=(\S+) workers_ms=(\S+) pacing_ms=(\S+)', log)
                if start_ms <= int(m[2]) < end_ms]
        scene['slow_frames_24ms_threshold'] = [{label: float(m[index]) for index, label in enumerate(
            ('frame', 'time_ms', 'total_ms', 'move_ms', 'camera_ms', 'render_ms', 'workers_ms', 'pacing_ms'), 1)} for m in slow] if require_trace else None
    return dict(name=name, process=result, scenes=scenes,
        profile_sha256=hashlib.sha256((root / 'profile-initial.ltx').read_bytes()).hexdigest(),
        controller_sha256=hashlib.sha256((root / 'controller.script').read_bytes()).hexdigest(),
        legacy_shader_compile_failure_lines=log.count('Failed to compile'),
        fatal_or_device_error_lines=[line for line in log.splitlines() if re.search(r'FATAL ERROR|DXGI_ERROR_DEVICE_|Device removed|NVRHI.*(?:ERROR|Error)', line)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--off', default='DX12_PARALLEL_CONTROLLED_OFF_041_AP')
    parser.add_argument('--on', default='DX12_PARALLEL_CONTROLLED_ON_041_AO')
    parser.add_argument('--name', default='MISERY_DX12_PARALLEL_CONTROLLED_SUMMARY')
    parser.add_argument('--without-trace', action='store_true')
    args = parser.parse_args()
    assert re.fullmatch(r'[A-Za-z0-9_]+', args.name)
    runs = {mode: read_run(name, require_trace=not args.without_trace) for mode, name in (
        ('off', args.off), ('on', args.on))}
    assert runs['off']['process']['sha256'] == runs['on']['process']['sha256']
    for field in ('profile_sha256', 'controller_sha256'):
        assert runs['off'][field] == runs['on'][field]
    clock_differences = {}
    for case in CASES:
        a, b = (runs[mode]['scenes'][case] for mode in ('off', 'on'))
        for field in ('pos', 'dir', 'lens'):
            av = [float(v) for part in a[field] for v in part.split(',')]
            bv = [float(v) for part in b[field] for v in part.split(',')]
            assert len(av) == len(bv) and all(abs(x-y) <= .001 for x, y in zip(av, bv))
        def minutes(value):
            hour, minute = map(int, value[0].split(':'))
            return hour * 60 + minute
        clock_differences[case] = abs(minutes(a['clock']) - minutes(b['clock']))
        assert a['clock'][1] == b['clock'][1] and clock_differences[case] <= 1
        print(case, 'API fps off/on', *(round(runs[m]['scenes'][case]['application_fps'], 2) for m in ('off', 'on')))
        if not args.without_trace:
            print('CPU renderer ms off/on', *(round(runs[m]['scenes'][case]['timings_ms']['cpu_stage']['RendererTotal']['mean'], 3) for m in ('off', 'on')))
    trace_note = ('CPU/GPU/slow-frame trace switches disabled in both; no worker or CPU-stage timing samples requested.' if args.without_trace else
                  'CPU trace, GPU timestamp profiler and >=24ms frame trace enabled in both. Actual traced quality is FXAA/AO-high.')
    summary = dict(runs=runs, method='Same binary, starting profile, save and controller; four 30-second windows trimmed by 0.5 seconds at each end. '+trace_note+' PresentMon application API intervals only; display mode, displayed-frame timing, latency and drops unavailable. One sequential run each; simulation may differ. Per-scene GPU sample counts and timestamps describe coverage. No claim of displayed-frame speedup or complete graphics parity.',
        clock_differences_minutes=clock_differences,
        decision='Keep priority 1 active and parallel recording opt-in. Separate shadow/caster optimization remains deferred.')
    path = EVIDENCE / (args.name + '.json')
    path.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(path)
