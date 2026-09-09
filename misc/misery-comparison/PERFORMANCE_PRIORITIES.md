# DX12 engine performance priorities

Research and source review: 2026-09-09 UTC. Updated with the first native parallel-recording experiment below; it remains disabled by default. Inspected source: `0a754d0127956cef570176e5b88801a415b9b19f`. The baseline at that research checkpoint was 0.41, compiled from `74a3749453254d6b80f68c695fa378373e5ca656`. CPU stage/pass tracing was subsequently implemented and tested on source parent `86742e6580092c0646562b7f67b41627ac7a3d09`. No faster rendering path was accepted at this checkpoint.

The user's priority is to remove internal engine bottlenecks so native x64/DX12 can use modern hardware properly. Put that work ahead of further graphics additions. Retain the current visual quality, complete MISERY behavior, conventional shadows, 16x filtering and RTX HDR-compatible SDR output. Ray tracing remains excluded.

User correction after the failed submission experiment: do not skip from submission timing to separate shadow optimization. Finish the parallel command-recording architecture first. Shadows may supply its first workload; that does not authorize prioritizing shadow-specific culling or draw optimizations ahead of it.

## Priority 1: culling preparation checkpoint (2026-09-09 UTC)

The collector now maintains ordered skinned-batch indices while retaining the level-static prefix. The skinned upload visits those entries instead of scanning the whole scene. It preserves static skinned visuals and submission order. Mutable access or sorting invalidates the index and uses a full-scan fallback until `EndFrame` rebuilds it. Rigid, terrain and skinned culling now reuse the existing binding-set cache, as their compaction passes already did. No shader, visibility criterion, shadow algorithm, quality or simulation callback changed.

The pinned NVRHI `Externals/nvrhi/doc/ProgrammingGuide.md` distinguishes bindings, which retain resource references, from volatile constant-buffer contents, which are written in command-list order. The existing cache keys include the resources, layout and view parameters; resize clears cached binding sets and retired textures release matching references. This reuses those existing lifetime mechanisms.

Candidate executable `c0544acd2cda9d6c9b4fa320de58fbd646a18d1ddeac50e9581fdb5e8ce6add7` was built from parent `37ea59d0b933cc4652d67956637f0e28975f064c` plus archived patch `ca0668212aac7b871137b3d5dd6a591f99e250e70c59026d2eff555b62e4357f`. Matching PDB, exact source-file hashes and build logs are in workspace `work/runtime/cpu-culling-preparation-043`. The 0.43 play package remains unchanged.

AX (`DX12_CULL_PREP_BASELINE_043_AX`, 0.43 executable) and AY (`DX12_CULL_PREP_CANDIDATE_043_AY`) completed the existing four-scene comparison at 3440x1440 with matching initial profiles, controller, replays, cameras and clocks. Both use FXAA, AO-high, 16x filtering, conventional/grass shadows, FG off and parallel recording on. CPU/GPU/slow-frame tracing is enabled in both. The index-equivalence diagnostic is off during these timings. All game, PresentMon and GPU-sampler exits are 0.

| Scene | Skinned culling CPU ms before / after | Total renderer CPU ms before / after | Application FPS before / after | Application p99 ms before / after |
|---|---:|---:|---:|---:|
| Interior | 0.3262 / 0.2909 | 6.1428 / 6.1714 | 96.37 / 94.90 | 14.88 / 14.72 |
| Outdoor | 0.2941 / 0.2739 | 5.4826 / 5.5062 | 89.64 / 91.34 | 14.73 / 14.77 |
| Rain | 0.2880 / 0.2570 | 5.4179 / 5.4246 | 93.72 / 95.12 | 14.70 / 14.91 |
| Night | 0.2544 / 0.2109 | 5.6532 / 5.6725 | 92.14 / 92.57 | 15.42 / 15.41 |

The skinned pass is 0.020-0.044 ms cheaper in this pair. Ordinary culling changes by only 0.002-0.005 ms. **No total-renderer or consistent FPS/p99 improvement is established.** Game workloads differ, including transient additional lights/faces in AY. Each window has fourteen GPU samples; mean NVIDIA utilization is 95-97%, with sampled background engines at most 0.327%. PresentMon records application API intervals, not displayed-frame timing, mode, latency or drops. All eight stills were inspected without an obvious new rendering regression. Each run retains 836 legacy shader failure lines, with no matched fatal/device/NVRHI error.

AZ (`DX12_CULL_PREP_RELOAD_043_AZ`) completed two full Zaton unload/reloads with native D3D12 debug/DRED and NVRHI validation, parallel recording, DLSS Quality and FSR FG. The optional `-geometry_batch_check` matched every skinned index and its order against a full scan in fourteen samples, including a load-time empty skinned set and populated sets of 191-192. Thirty-three worker samples have distinct threads and positive overlap. Exit 0; last successful FG dispatch count 1731. All 153 inventory entries retain contents, condition and ammo; raw equality is false only because bolt ID 11077 becomes 14848. Grouse's 22,927-byte custom data is exact after both reloads. All three stills retain terrain, grass and the weapon; changed clouds across reload do not establish exact weather parity. There are 2216 legacy shader failures and no matched fatal/native/NVRHI error. Different-level transitions, long sessions and generated-frame temporal quality remain unqualified.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_CULL_PREPARATION_043_SUMMARY.json` and each run's `analysis.json` or `reload-analysis.json`. The unique reload files are archived, its control restored, and normal 0.41 staging executable/PDB/profile restored and hash-verified. Retain this bounded preparation change; keep parallel optional. Next, inspect the remaining repeated static-material classification in rigid upload preparation before another performance run. Separate shadow/caster optimization remains deferred.

## 0.43 delivery and native-resolution qualification (2026-09-09 UTC)

Delivered independent package `D:\Codex\MISERY-DX12\outputs\MISERY_DX12_DEV_0.43`, with 17,170 initially SHA-verified files / 11,453,744,227 bytes. Compiled source is `a70a2ee050f16f04432e86a7b6db012046e4d86f`; the actual package log reports that identity. Executable SHA-256 is `39a6c9683fef395ec749d5f539312d76b0c42d1eae50ad8fa3a1cf1a2d27e2e9`, with matching archived PDB. This delivers the retained-static geometry change below, the session-only recording control and optional low-rate workload counts. The normal launcher remains serial; parallel is optional.

AV (`PACKAGE_043_NATIVE_SERIAL_AV`) and AW (`PACKAGE_043_NATIVE_PARALLEL_AW`) used the actual package, 3440x1440, FXAA, AO-high, 16x, conventional shadows/grass and FG off. Initial profiles, controllers and camera replays match. Detailed CPU/GPU timers and slow-frame tracing were off; `-workload_trace` logged counts once per second. Both game, PresentMon and GPU-sampler exits were 0. All eight stills were inspected; no new visible corruption was noted. Each run retains 836 legacy shader failure lines, with no matched fatal/device/NVRHI error. These checks do not establish complete effects or campaign stability.

| Scene | Application FPS serial / parallel | p99 ms serial / parallel |
|---|---:|---:|
| Interior | 101.81 / 101.41 | 13.48 / 13.91 |
| Outdoor | 93.47 / 91.44 | 14.78 / 14.54 |
| Rain | 97.59 / 98.60 | 14.06 / 13.68 |
| Night | 113.11 / 118.54 | 13.01 / 12.21 |

No consistent native FPS or p99 gain is established for parallel recording. Game GPU utilization averages about 94-97%; sampled background engines remain below 0.32%, with fourteen GPU samples per scene. Workload counts differ: serial night lights span 39-46, parallel 43-45; clocks differ by one minute in rain/night. These samples cannot assign the night difference to recording, especially with the prior traced sequential fallback. Earlier CPU measurements remain the evidence for the CPU savings; do not reinterpret these application timings as displayed-frame or isolated-code speedups. API-only PresentMon has no display-mode/latency/drop evidence.

The refreshed `outputs/MISERY_DX9_DX12_COMPARISON_043.html` retains the fixed `DX9_MATCHED_041_B` baseline and uses AV for the sliders. `MISERY_GRAPHICS_SETTINGS_043.json` updates actual profile/capture provenance; all eleven cited implementation/reference file hashes were rechecked unchanged. The report includes AW's native comparison and the data limitations. Eight native-sized images, four sliders, seventeen local links and script syntax were checked. No new browser screenshot was taken.

Package normal profile restored; all 17,170 files checked for presence/size after testing and the five modified/critical binary/profile/save hashes verified. Prior packages and the live/reference installations remain preserved. Separate normal 0.41 staging executable/PDB/profile restored and hash-verified. Source/build archive: `work/runtime/workload-package-043`. Native evidence: `outputs/implementation-evidence/MISERY_DX12_NATIVE_PARALLEL_043_SUMMARY.json`. Delivery details: `DEVELOPMENT_043.md`.

Next: stay on priority 1 and use the existing CPU traces to address remaining geometry-data preparation/recording costs. GPU Culling and Skinned GPU Culling each recorded about 0.27 ms CPU in the earlier AT serial daytime trace. Inspect those concrete paths before another benchmark; do not keep repeating unchanged native FPS runs. Separate shadow/caster optimization remains deferred.

## Priority 1: static geometry CPU work (2026-09-09 UTC)

The current candidate removes repeated destruction and copying of 21,786 unchanged static batches. The collector retains its static prefix and rebuilds dynamic batches after it. Full level unload clears the collector and resets the prefix before destroying level visuals. Shadow algorithms, culling criteria, shaders, quality settings and game callbacks are unchanged. This also removes the redundant second static-batch vector.

`fg_parallel_record 0/1` now switches the experimental recorder within one session. Startup still follows `-fg_parallel_record`; the live command does not save itself to `user.ltx`. Normal launches remain serial. `-cpu_trace` now measures collection substages and samples geometry/light/particle workloads once per second. The `sun` workload field denotes a valid sun-map resource, not whether a sun draw pass executed.

For native-resolution qualification, `-workload_trace` also permits those once-per-second counts with CPU/GPU pass timing disabled. Its output names that resource field `sun_map` explicitly. Keep this low-rate logging disclosed in results; do not call it a fully uninstrumented run.

AS (`DX12_PARALLEL_CROSSOVER_042_AS`) and AT (`DX12_STATIC_RETAIN_CROSSOVER_042_AT`) ran the same eight-window controller, alternating OFF/ON/ON/OFF by day and ON/OFF/OFF/ON at night. Each window lasts 15 seconds after settling. Both use 1720x720, FXAA, AO-high, 16x, FG off and identical initial profile/controller hashes; cameras agree within 0.001 and recorded clocks match. Native DX12, ordinary game callbacks and NVRHI validation remain enabled. Both games, PresentMon and GPU samplers exited 0.

Combined `CollectorBegin` and `StaticGeometry` CPU work fell from 0.485-0.491 ms to 0.0036-0.0048 ms per frame across the eight windows. Daylight total renderer CPU time improved by 0.36-0.52 ms. Sampled static count remained 21,786. Twenty-eight daytime samples in each run used distinct overlapping recording threads; night used serial fallback. Sampled background GPU engines stayed below 0.16%.

| Window | Renderer CPU ms before / after | Application FPS before / after |
|---|---:|---:|
| Day OFF A | 6.402 / 5.940 | 117.63 / 123.22 |
| Day ON A | 5.605 / 5.242 | 124.20 / 126.54 |
| Day ON B | 5.605 / 5.117 | 123.82 / 127.85 |
| Day OFF B | 6.491 / 5.968 | 115.60 / 121.88 |
| Night ON A | 5.288 / 4.903 | 138.24 / 147.35 |
| Night OFF A | 5.286 / 5.280 | 138.37 / 135.09 |
| Night OFF B | 5.056 / 5.040 | 143.91 / 135.24 |
| Night ON B | 4.960 / 4.962 | 146.91 / 138.29 |

These are traced, lower-resolution application timings, not native-resolution or displayed-frame gains. Night workload varies materially: AS lights fell from 45 to 39, while AT later reached 49. The same-process check did not reproduce a consistent penalty following the recorder switch; it does not prove the cause of the older AQ/AR difference. Day ON A's p99 also rose from 11.37 to 11.95 ms during a brief increase in lights/faces/particles. Do not hide these differences or claim a universal FPS/tail improvement.

AU (`DX12_STATIC_RETAIN_RELOAD_042_AU`) completed two full Zaton unload/reloads at 3440x1440 output with DLSS Quality, FSR FG, parallel recording and native debug validation. Exit 0; three cache builds each contain 21,786 batches; 33 sampled groups overlap on distinct threads. All three stills retain world geometry, terrain and grass. All 153 inventory entries retain contents/condition/ammo; raw identity differs only for bolt 11077 -> 14848. Grouse's 22,927-byte custom data is exact after both reloads. Last successful FG dispatch count is 1724. This is not different-level or extended-session qualification.

AS/AT each retain 836 legacy shader compilation failure lines; AU retains 2216 across three loads. No matched fatal, native D3D12 validation or NVRHI error occurred. Two baseline stills, four optimized crossover stills and all three reload stills were inspected; this is bounded visual evidence, not complete shader/effects parity.

Candidate executable SHA-256: `53bee745f1e0125bd33d3d37254f8ca4e22d1547af7f3ba12ad6b78d4a1b0614`. Matching PDB and exact source patch are retained in workspace `work/runtime/static-geometry-retain-042`. Full evidence: workspace `outputs/implementation-evidence/MISERY_DX12_STATIC_GEOMETRY_CPU_SUMMARY.json`. Normal 0.41 staging executable/PDB/profile were restored and hash-verified; delivered 0.42 and the fixed DX9 comparison remain unchanged.

Next: remain on priority 1. Measure practical native-resolution performance with instrumentation costs explicit and scene workloads accounted for before default parallel enablement. Separate shadow/caster optimization remains deferred.

## 0.42 delivery and CPU scaling (2026-09-09 UTC)

Development package 0.42 is delivered at `D:\Codex\MISERY-DX12\outputs\MISERY_DX12_DEV_0.42`. It uses the tested engine source `e54ae6076994b1302fca6936c4238c7aa5da7de9` / executable `d217d8cda0cd688ff83907841c871e450ff957e2ff4ad2b36cb8d56ab6817ddb`, with the already-qualified resize/reload/resource-state fixes. No further engine/shader change was made in this checkpoint. The normal launcher remains serial; the separate experimental launcher adds only `-fg_parallel_record`. Shadow-specific optimization remains deferred.

AQ (OFF) and AR (ON) used 1720x720, the same ultrawide aspect ratio as 3440x1440, and identical profile/controller hashes between modes. Resolution is the only edit to the preserved normal starting profile. All effects, FXAA, filtering and simulation settings remain; CPU/GPU/slow-frame trace switches were absent. Both games, PresentMon and GPU samplers exited 0. Each trimmed scene has fourteen during-run GPU samples. GPU use ranged from roughly 73-94%; sampled external-engine utilization stayed below 0.47%. The interior recorded clocks differ by one minute; other clocks and all cameras match.

| Scene | Application FPS OFF / ON | p99 ms OFF / ON |
|---|---:|---:|
| Interior | 104.11 / 116.99 | 14.20 / 12.81 |
| Outdoor | 122.01 / 127.92 | 11.71 / 11.21 |
| Rain | 122.48 / 132.37 | 12.32 / 11.16 |
| Night | 152.25 / 144.04 | 10.48 / 11.03 |

Daytime throughput improves by 4.8-12.4% in this untraced pair, while night is 5.4% slower despite sequential fallback. This is mixed evidence, not default-enablement qualification or a claim of universal speedup. The night difference remains unresolved. No worker/CPU-stage timing samples were requested in these runs; prior instrumented runs establish overlap. PresentMon captures application API intervals only, with display mode/timing/latency/drops unavailable. Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_CPU_SCALING_SUMMARY.json`.

Packaging initially omitted the four camera replay fixtures, causing `PACKAGE_042_DX9_BASELINE` to render wrong views. Its owned process was stopped, exit -1, and that run is rejected as a comparison. The package now includes the verified replays; the package builder copies them and the comparison runner rejects absent/empty native fixtures before launch, recording their hashes. `PACKAGE_042_DX9_BASELINE_B` then completed all four matched scenes at 3440x1440 with the normal FXAA profile and exited 0. Native application FPS: 98.38 / 90.63 / 96.15 / 114.60. No matched fatal/device/NVRHI errors; 836 legacy shader compile failures remain. Four screenshot dimensions and camera markers checked; interior and night stills inspected. This remains bounded runtime evidence, not complete graphics/campaign/long-session proof.

Refreshed report: workspace `outputs/MISERY_DX9_DX12_COMPARISON_042.html`, using the preserved `DX9_MATCHED_041_B` baseline. It explicitly records DX12 FXAA versus DX9 AA-off, other implementation/cost differences, and missing DX12 display tracking. Historical 0.41 report/cost data remain separate. Normal 0.42 packaged profile and normal 0.41 staging files are restored; source/replay/package hashes retained. Previous packages and the original installation are preserved.

Next: priority-1 CPU critical-path work and resolution of night-sample variation before default parallel enablement. Use the existing trace and matched replay inputs to distinguish draw/simulation workload changes from recorder overhead. Do not pivot to shadow-specific culling/quality changes. The full port goal remains active.

## Controlled priority-1 measurements (2026-09-09 UTC)

Priority 1 remains active; parallel recording remains opt-in. Candidate source is `e54ae6076994b1302fca6936c4238c7aa5da7de9`, executable SHA-256 `d217d8cda0cd688ff83907841c871e450ff957e2ff4ad2b36cb8d56ab6817ddb`. No engine or shader code changed in this checkpoint. Separate shadow/caster optimization is still deferred.

AO (parallel ON) and AP (OFF) used the same binary, starting-profile and controller hashes, the same save, matching cameras/clocks/weather, native 3440x1440, FXAA, AO-high, 16x filtering and FG off. CPU tracing, GPU timestamp profiling and slow-frame tracing were enabled in both. Both games and PresentMon exited 0 after all four scenes. All 84 daytime worker samples used distinct threads with positive overlap; night used the sequential fallback.

| Scene | CPU renderer ms OFF / ON | Application FPS OFF / ON | Application p99 ms OFF / ON |
|---|---:|---:|---:|
| Interior | 7.418 / 6.614 | 95.29 / 95.72 | 14.28 / 14.85 |
| Outdoor | 6.743 / 5.985 | 89.23 / 91.64 | 15.42 / 15.00 |
| Rain | 6.735 / 5.883 | 92.15 / 94.85 | 15.68 / 14.58 |
| Night | 5.947 / 6.037 | 93.47 / 93.11 | 14.69 / 15.49 |

The three daytime scenes reduce CPU renderer time by 0.76-0.85 ms (about 11-13%). Their CPU Execute time falls by 0.75-0.82 ms; sampled job overlap averages 1.51-1.55 ms. Application FPS differs by +0.5% to +2.9%, while night is -0.4%. These are one sequential instrumented run each, not a proven uninstrumented or displayed-frame speedup. Keep the opt-in flag and existing play package.

PresentMon's normal display-tracking path produced no CSV in AJ. Short preflights AK/AL and AM/AN isolated a usable application-API capture path with `--no_track_display --no_track_gpu`. The comparison runner now has an explicit `-PresentApiOnly` switch and records the unavailable tracking fields; its normal path is unchanged. AO/AP capture application present intervals only. Presentation mode, displayed frames, display latency and dropped-frame statistics are unavailable, so these measurements do not qualify a presentation-parity claim. The reason the display-tracking path failed remains unresolved. The existing DX9 report and its captured data were not rewritten. The [PresentMon project documentation](https://github.com/GameTechDev/PresentMon#troubleshooting) also distinguishes ETW access from the warning about process metadata; this account already has Performance Log Users membership and the same warning occurred in earlier successful captures.

During-run NVIDIA and per-process GPU counters show roughly 92-97% game/overall GPU use. No sampled external engine exceeded 0.32% inside these measured windows. This supports GPU saturation as a likely limit on native-resolution gains; it does not prove the absence of shorter external activity. AO interior telemetry starts 16.7 seconds into that 30-second window (7 samples); the other windows have 14 samples each. One impossible AO night counter was rejected without altering the raw file; the original AO sampler did not record counter Status, so status-based validity is unknown there. AP records Status-related rejection counts. Both initial sampler versions returned code 1 during process-exit handling after the measured windows. That shutdown handling was then corrected and checked against a six-second owned idle process, which exited the sampler normally; this was a helper check, not another gameplay benchmark.

Both full runs retain 836 existing legacy shader compilation failures and no matched fatal/device/NVRHI error. The outdoor ON and OFF screenshots were inspected: terrain, grass and the overall lighting appearance remain present, without an obvious new difference in these stills. This is not full visual/effects parity or extended stability certification. The >=24 ms frame trace also shows isolated game-update and worker-wait stalls; parallel rendering does not remove every engine bottleneck.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_PARALLEL_CONTROLLED_SUMMARY.json`, with raw AO/AP data, CPU/pass distributions, slow frames and telemetry. AJ and AK-AN are retained as incomplete capture/diagnostic evidence, not completed FPS benchmarks. The exact runner, sampler and analysis helpers are versioned here; deploy them into the existing workspace `work` directory before use. Normal 0.41 executable/PDB/profile were restored and all three backup hashes verified. No new package, baseline replacement or shadow-specific optimization.

Next: priority-1 CPU scaling measurement at a lower resolution in the isolated test profile, with all effects and simulation settings equal between modes. Record application frame times without CPU/GPU tracing and retain during-run telemetry. Keep the native-resolution measurements as the practical baseline, restore the normal profile afterward, and avoid claiming displayed-frame gains until display tracking works.

## In-process level reload checkpoint (2026-09-09 UTC)

Priority 1 remains active and opt-in. The existing save checks used separate processes, so a small additional controller now saves to a unique isolated slot and queues two full disconnect/reloads in one process. A same-level `load` uses the quickload path and would not exercise complete renderer level teardown. Normal MISERY callbacks continue throughout. Separate shadow/caster optimization remains priority 2.

The first full reload exposed missing cleanup. The detail manager accumulated 37 authored models into 74 and hit the packed 64-model limit (AC). Calling its existing unload after draining GPU work fixed that failure, but the next rendered frame accessed cached visuals from the old level (AD). Those cached batches now invalidate at teardown. Subsequent runs exited cleanly but screenshots exposed missing terrain and grass (AE/AF). Terrain draw-data and detail initial-upload flags now reset with the level; this restored terrain while grass still disappeared (AG). Complete detail shader cleanup prevents loading frames from constructing an incomplete graphics pipeline from retained blade shaders before billboard shaders reload. Owned detail texture slots retire after the GPU wait, and grass generation state resets. No per-gameplay-frame wait, simulation reduction, visual-quality reduction or shadow algorithm change was added.

Final candidate SHA-256: `d217d8cda0cd688ff83907841c871e450ff957e2ff4ad2b36cb8d56ab6817ddb`, retained with PDB and exact source patch in workspace `work/runtime/detail-shader-reload-041`.

| Run | Configuration | Result |
|---|---|---|
| `DX12_INPROCESS_RELOAD_041_AH` | Parallel recording, DLSS Quality, FSR FG, native debug layer and `-cpu_trace` | Two full Zaton reloads and exit 0. Thirty-three sampled groups used distinct threads with positive overlap. Each load has 37 detail models, uploads 1439 terrain draw records, and uploads the new detail data. No native validation or NVRHI error. Last FG dispatch count 1671. |
| `DX12_INPROCESS_RELOAD_041_AI` | Same binary and profile, parallel recording; native diagnostics and CPU tracing off | Two full reloads and exit 0; no fatal/device/NVRHI error. Last FG dispatch count 2063. No worker samples were requested in this run. |

Both post-reload screenshots from each final run were inspected: terrain and grass remain visible. Earlier AE/AF/AG visual failures are retained and are not counted as passes merely because the processes exited 0. Each final run preserves all 153 inventory entries' contents, condition and ammo, plus the complete 22,927-byte Grouse custom data. Raw inventory equality is false: bolt ID 11077 becomes 14848 after reload. All other identity rows match.

Both final logs retain 2070 legacy shader compilation failures (690 per level load); loading frames can also log that the detail graphics pipeline is not ready before its shader set reloads. This is bounded lifecycle qualification, not complete effects parity, different-level transition coverage, extended memory/eviction testing or generated-frame temporal-quality certification. Cloud/weather appearance changes after saving/loading were not assessed as exact weather parity. No FPS gain is claimed.

The versioned `dx12_inprocess_reload_041.script` reads a unique lowercase save name from the isolated profile's `_appdata_/dx12_inprocess_reload_041_run.txt`. Start with no state files or save for that name and use the existing `run-graphics-comparison.ps1` with `-ScriptNamespace dx12_inprocess_reload_041`. Each evidence folder preserves that name, the exact controller, save, snapshots, executable identity, log, profiles and screenshots. Summary: workspace `outputs/implementation-evidence/MISERY_DX12_INPROCESS_RELOAD_SUMMARY.json`.

Next: obtain controlled CPU critical-path and frame-time measurements before promoting parallel recording. Recheck current background GPU use and sample it during the run; the no-game queries at this checkpoint's start and end both showed 0% utilization. Broader lifecycle and gameplay qualification remain unfinished. Normal 0.41 executable/PDB/profile were restored and hash-verified. The original installation, fixed DX9 reference, delivered package and published comparison were not changed in this checkpoint.

## Resize reliability checkpoint (2026-09-09 UTC)

Priority 1 remains active. This fixes a reproduced integration failure encountered while qualifying parallel recording; separate shadow/caster optimization remains priority 2. Candidate SHA-256 `c9665f2de363199ce5ae32c1f212aa19ad494e28abdd6e9b3ef8983d9e77d52d` is preserved in workspace `work/runtime/resize-material-lifetime-041`. The delivered package remains 0.41 and parallel recording remains opt-in.

The final change that let the existing resize sequence finish preserves the world material cache across resolution changes. Its textures and bindless indices do not depend on resolution, and persistent GPU material buffers still reference them. Framebuffer-dependent and UI caches still invalidate. GPU work now drains before reset teardown. This removes unnecessary world material destruction/reloading during resize, without adding a gameplay-frame wait or changing shadow algorithms or quality.

Native diagnostics also identified and corrected three state errors: NGX/FFX handoffs now bridge through COMMON; FSR owns the HUD-less target through presentation with COMMON as its initial/final state; and particles use a separate initial depth snapshot when previous depth is absent, instead of sampling their active depth attachment. Texture uploads now restore the declared resource state before later bindless draws in the same frame. A one-line CStalkerOutfit script-export prerequisite fixes the startup failure seen in S. These changes retain the actual effects and game callbacks.

| Run | Configuration | Observed result |
|---|---|---|
| `DX12_RESIZE_LIFETIME_041_Z` | Serial recording, native debug layer/DRED, no generated-frame readback | All five cases, 3440x1440 to 1920x1080 and back with DLSS Quality/FSR FG, then FXAA/FG off. Exit 0; no native validation or NVRHI errors. Last successful FG dispatch count 1138. |
| `DX12_RESIZE_PARALLEL_041_AA` | Same binary, `-fg_parallel_record -cpu_trace`, native debug off, NVRHI validation on | All five cases and exit 0. Sixty sampled parallel groups had distinct threads and positive overlap. Last successful FG dispatch count 2184. No fatal/device/NVRHI error. |
| `DX12_UPLOAD_GBV_041_AB` | Same binary, native GPU-based validation, normal gameplay | Controller completed frames 35 through 155 and exited 0. No native validation or NVRHI errors, including the COPY_DEST sampling errors previously seen in X. This checks initial world/UI uploads, not a full resize sequence under GPU validation. |

All three logs retain 690 existing legacy shader compilation failures. Three Z and two AA stills were reviewed without obvious new material/lighting corruption; this does not verify all generated frames, temporal quality or complete effects parity. No FPS improvement is claimed from these functional runs.

Q through Y preserve the failed attempts. The NVRHI combined-depth-layout backport did not fix the particle feedback problem and was removed; its pinned submodule is clean. X was stopped after GPU validation exposed upload-state errors, before its resize controller began. Its pre-existing controller file was not treated as new evidence. The repeated command-list/allocator errors around device removal may be secondary and are not a proven root cause. Summary: workspace `outputs/implementation-evidence/MISERY_DX12_RESIZE_INTEROP_SUMMARY.json`.

The COMMON handoffs follow Microsoft's [enhanced-barrier interoperability specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html). Diagnostics use [DRED](https://learn.microsoft.com/en-us/windows/win32/direct3d12/use-dred); breadcrumb locations identify unfinished work rather than proving the exact offending instruction. FSR presentation/lifetime handling was checked against the [pinned FidelityFX API guide](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/60f4ea81909200d8542eca14dccb2628b763a9a3/Kits/FidelityFX/docs/techniques/frame-interpolation-api.md).

Next: continue priority-1 in-process level reload/lifetime coverage, then obtain controlled CPU/frame-time measurements before promoting parallel recording. Inspect current background GPU use and sample it during performance runs. General bindless retirement during eviction/level teardown, broader gameplay, full effects parity and net FPS benefit remain unqualified. Normal 0.41 executable/PDB/profile were restored and hash-verified; original installation, fixed DX9 reference, delivered package and published comparison were not changed in this checkpoint.

## Work order and current evidence

| Order | Engine work | Verified finding and next implementation step |
|---|---|---|
| 1 | Parallel CPU command recording and timely submission | Delivered 0.41 records sequentially. The opt-in `-fg_parallel_record` path owns separate lists/contexts and records certified sun/local callbacks concurrently. Its first preparation bottleneck was measured and reduced from 2.48 to 0.25 ms in the short probe. The subsequent untraced pair did not establish a net FPS gain; substantial background GPU use was observed afterward. Retain the preparation fix, verify CPU critical-path benefit and controlled frame times before promotion or priority 2. |
| 2 | Reduce repeated shadow and visibility work | The completed feature probe measured local-shadow differences of 1.12/1.50 ms and sun-shadow differences of 0.70/0.91 ms when grass shadows were disabled at fixed 8x filtering in the steadier interior/outdoor sequences. Keep grass shadows enabled and target their implementation cost. Trace light/caster visibility before generating faces, repeated candidate scans, indirect argument preparation and vegetation work. Native light collection at `r_FrameGraphRenderer.cpp:2668` does not perform a portal-visibility check in that branch; whether this causes avoidable shadow work is still a hypothesis. Preserve offscreen casters, wind and moving-character shadows. The static local-depth cache already works. |
| 3 | Restore reliable DX12 async compute and useful queue overlap | `r_FrameGraphRenderer.cpp:522` explicitly wires async compute only for Vulkan; its comment cites D3D12 device removal. That failure was not reproduced during this research, and its root cause is unproven. Trace resource states, cross-frame reuse and actual queue submissions before enabling DX12. The dormant D3D12 wait helper calls an immediate queue wait through NVRHI; calling it while recording one giant list does not place a wait inside that list. Correct submission boundaries are part of this work. |
| 4 | Remove first-use stalls and repeated CPU setup | `PipelineStateCache::GetOrCreate` creates a missing PSO synchronously while holding its cache lock (`RenderContext/PipelineState.cpp:222`). The frame graph rebuilds dependencies, lifetimes and aliasing decisions each frame (`FrameGraph/FrameGraph.cpp:190`). Measure these costs and cache misses; prewarm required pipelines and reuse stable preparation where those measurements justify it. Existing shader, material, binding and texture caches must be reused. A scoped search found no D3D12 pipeline-library creation in the renderer/NVRHI code reviewed; this is not a claim that all disk caching is absent. |
| 5 | Memory, uploads and actual capacity limits | `ResourceManager/TextureManager.cpp:551` already derives its allowance from Windows GPU-memory budgets; it refreshes once per second. The old 2 GiB value is a fallback, not the active RTX 3090 limit. Target allocation churn, upload stalls, residency pressure and demonstrated content ceilings. `ClusteredLightManager.h:44` has 1024-light and 768-shadow-face capacities, but every local-shadow trace in the latest probe reports zero omitted lights. Raising those capacities is not presently a demonstrated FPS optimization. Expand a capacity when content requires it, with correctly sized buffers, indices and shaders. |

These priorities distinguish a confirmed structural limitation from a measured cost and from a hypothesis. The existing CSV now supports opt-in `-cpu_trace` records for renderer stages and individual pass callbacks. These are nested CPU wall times, including any waits inside those regions; they do not isolate AI/physics, the full game-thread critical path or GPU idle gaps. The full CPU profiler was not globally enabled. Broader game-thread or SIMD changes should follow hot-path evidence.

## First owned parallel-recording implementation (experimental)

Implemented persistent worker command lists and recording contexts, dependency-ordered frame segments and one final NVRHI submission. Shared skeleton/material/decal uploads precede every worker list; shared lazy caches and GPU query bookkeeping are synchronized. Each list receives its own volatile frame constants. The existing task scheduler executes the certified sun/local callbacks concurrently. This changes recording architecture, with the existing shadow algorithms and quality controls retained.

Enable only with `-fg_parallel_record`. It requires DX12, at least two available workers, no active async-compute path and the full CPU profiler disabled. The narrow `-cpu_trace` remains compatible. If fewer than two eligible jobs exist, recording stays sequential; the night comparison exercised that path. This covers the first two substantial callbacks, not the whole renderer.

Candidate executable SHA-256: `75dd2c218427bf4eef2f9f4a294d6d887814629d0c959f2ecc5533cc62d7754b`, built from parent `e02d3d31c57c6edc8ee656ec6e108399068f06b2` plus the archived source patch. `DX12_PARALLEL_ACTIVE_041_C` proved distinct worker threads. The full `DX12_PARALLEL_FULL_041_D` and same-executable `DX12_PARALLEL_OFF_041_E` each completed interior/outdoor/rain/night and exited 0 with NVRHI validation enabled. All eight captures were inspected with no obvious new lighting/shadow failure in these views. Both logs retain 836 existing shader compilation failures, zero sampled omitted shadow lights and no matched fatal/device/NVRHI error. This does not establish extended stability or complete effects parity.

| Scene | FPS off / on | p99 ms off / on | CPU Execute ms off / on | Mean job overlap ms |
|---|---:|---:|---:|---:|
| Interior | 85.19 / 75.86 | 17.34 / 19.41 | 5.77 / 7.16 | 1.63 |
| Outdoor | 80.78 / 82.43 | 17.61 / 17.85 | 5.30 / 5.83 | 1.69 |
| Rain | 84.34 / 87.82 | 16.91 / 16.75 | 5.17 / 5.48 | 1.55 |
| Night | 86.01 / 90.31 | 16.90 / 19.46 | 4.61 / 4.58 | Sequential fallback |

**Decision: retain as an opt-in architecture experiment; do not promote to the play package or claim a performance gain.** Every daytime overlap sample used distinct thread IDs (86 samples in the trimmed windows), but total CPU Execute wall time increased by about 0.31-1.39 ms in those scenes. ON used Composed Flip; OFF used Hardware Composed Independent Flip, so their FPS differences do not isolate this code change. The night path did no parallel work yet its FPS differed by 5%, reinforcing the measurement limit.

Priority 1 remains active: separate the costs of serial preparation, scheduling/waits and command-list close/append from callback recording; then reduce demonstrated overhead in this architecture. Shared-cache contention is a hypothesis until measured. Repeat with matching observed presentation paths and without tracing before a speedup claim. Do not replace this with shadow-specific culling or caster-reduction work.

Evidence summary: workspace `outputs/implementation-evidence/MISERY_DX12_PARALLEL_RECORDING_SUMMARY.json`. Earlier A/B diagnostic runs did not activate parallel recording because `RenderDevice::BackendRef` lacked forwarding for the new backend methods; that was fixed before C/D/E. Their clean exits were not parallel-runtime proof. Normal 0.41 executable, PDB and play profile were restored and hash-verified after the pair. The candidate binary/PDB are retained separately in `work/runtime/parallel-recording-candidate-041`; original installation, fixed DX9 reference and delivered 0.41 package were not changed in this experiment.

## Parallel preparation follow-up (2026-09-09 UTC)

Added once-per-second `-cpu_trace` breakdowns for preparation, task launch/wait, command-list append, restored constants and individual worker open/record/close costs. The short `DX12_PARALLEL_COST_041_F` probe measured preparation at 2.476 ms, versus 0.037 ms for append. Inspection found that the newly added skeleton preparation scanned the full scene-batch collection separately for every shadow face.

Changed only that preparation: scan scene batches once per pass, reject non-skinned batches first, then test the original face/cascade visibility union and upload each required skeleton. Actual shadow drawing, visibility criteria, resolution, animation and grass coverage remain the same. `DX12_PARALLEL_PREPARE_041_G` measured preparation at 0.248 ms and the whole recording group at 3.468 ms, versus 5.496 ms before. Each figure is the mean of seven one-second samples inside the trimmed eight-second interior window. This is a CPU preparation improvement, not a claim of 90% faster gameplay. Both probes exited 0; their presentation modes differed.

Candidate SHA-256: `134098669f33f46d5ecd8b6e4bf2bde0c73f1862bd6d3f9ad6daa720a3a3e288`. The same binary then completed `DX12_PARALLEL_UNTRACED_ON_041_H` and `DX12_PARALLEL_UNTRACED_OFF_041_I`, four scenes each, without CPU/GPU tracing. Both exited 0 with NVRHI validation enabled, matching initial profiles/cameras/clocks/weather and Composed Flip throughout. No fatal/device/prepared-skeleton/bone-capacity error was matched; both retain the 836 pre-existing shader compilation failures. All four ON captures were inspected with no obvious new lighting/shadow failure in these views.

| Scene | FPS off / on | p99 ms off / on |
|---|---:|---:|
| Interior | 35.87 / 35.88 | 34.26 / 35.10 |
| Outdoor | 34.04 / 34.33 | 37.45 / 36.34 |
| Rain | 36.02 / 35.00 | 35.16 / 37.13 |
| Night | 41.91 / 36.41 | 30.46 / 34.20 |

These runs do not establish a net FPS benefit. They were unexpectedly much slower than the traced probes even with parallel mode off. Captured profiles match; the short and full controllers differ only in scene count and sample duration. A post-run check with the game closed found 64% total GPU use; three Windows GPU-counter samples attributed about 56.8% 3D-engine utilization and 13.8% video decode to Edge PID 15840. This is a concrete environmental confound, but it was not sampled during H/I and does not prove the cause or exact cost of their slowdown. No browser or other background application was controlled or stopped.

**Decision: retain the measured preparation fix; keep parallel recording opt-in and keep the delivered 0.41 baseline.** Before another FPS comparison, inspect background GPU use and capture it during the run; do not repeatedly benchmark against substantial uncontrolled GPU activity or assume a difference is caused by tracing. The existing `-frame_trace` can help distinguish engine render, game-update, worker and pacing waits if needed. Priority 1 stays ahead of separate shadow/caster optimizations. Broader parallel-path save/reload and temporal-AA/frame-generation coverage are still required before default enablement.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_PARALLEL_PREPARATION_SUMMARY.json` and `MISERY_DX12_PARALLEL_BACKGROUND_GPU.json`. Candidate executable/PDB retained in `work/runtime/parallel-recording-prepared-041`. Normal 0.41 executable/PDB/profile restored and hash-verified after the tests. The original installation, fixed DX9 reference and delivered package remain unchanged in this follow-up.

## Parallel compatibility follow-up (2026-09-09 UTC)

Candidate `13409866...` completed `DX12_PARALLEL_AA_FG_041_K` with parallel recording active through all 13 cases: FXAA, DLAA, DLSS Quality/Balanced/Performance, FSR frame generation off/on/re-enabled, NPC animation, camera motion, one verified weapon shot and rain. Every case contains four distinct-thread positive-overlap samples; the full log contains 114 parallel samples. Actual AA values and DLSS input/output sizes are retained, along with 13 screenshots and six generated-frame readback groups. Three representative screenshots and two generated frames were inspected without obvious new corruption; still images do not establish temporal quality. The game and PresentMon exited 0. The 836 existing shader compilation failures remain.

`DX12_PARALLEL_SAVE_041_L` and `DX12_PARALLEL_RELOAD_041_M` both exited 0, each with 12 parallel samples. The fresh-process reload retained all 153 inventory entries' contents, condition and ammunition, and the complete 22,927-byte Grouse custom data. Raw inventory equality is **false**: the bolt ID changed from 11077 to 14848; all other identity rows match. This difference is retained in the evidence, not silently discarded. These checks do not exercise in-process level teardown/reload.

The existing five-case resize probe then **failed** without generated-frame capture enabled. Candidate parallel ON (`DX12_PARALLEL_RESIZE_041_N`) logged device removal `0x887A002B` after recreation at 1920x1080; the helper stopped it at its deadline. Candidate parallel OFF (`DX12_SERIAL_RESIZE_041_O`) failed during the first downsizing with device removal `0x887A0006`; the agent stopped only the owned failed process. The preserved 0.41 executable (`DX12_RELEASE_RESIZE_041_P`, SHA-256 `4d3a3b36...`) also exited -1 after downsizing, with a stack containing NVIDIA-driver, AMD frame-generation and engine frames. A stack location does not establish whose code is at fault. No resize run completed, and no successful resize qualification is claimed.

This identifies a resize failure that also affects the released executable; it neither proves an identical root cause nor clears the new architecture. Earlier traced/capture resize successes are insufficient: generated-frame capture inserts additional waits, but whether that masks a lifetime race is unproven. Preserve the failing non-capture sequence and obtain native debug-layer/DRED evidence before selecting a fix. Keep this qualification issue separate from priority-2 shadow/caster optimization.

**Decision: priority 1 remains active and opt-in; no new package or FPS claim.** This follow-up changed no renderer code. Background GPU utilization was 68% with the game closed at the start, so these runs are functional evidence only. Normal 0.41 executable/PDB/profile were restored and hash-verified afterward; the candidate remains archived separately. Summary: workspace `outputs/implementation-evidence/MISERY_DX12_PARALLEL_FUNCTIONAL_SUMMARY.json`. The exact probe scripts and the existing save-check helper with an explicit `-Visible` switch are now versioned beside this document.

## CPU measurement and rejected submission experiment

The first instrumented baseline (`DX12_COMMAND_CPU_BASELINE_041_B`) measured about 5.0-6.3 ms for daytime sun plus local-shadow callbacks, versus about 0.04 ms for graph compilation. Existing one-second shadow traces put animated-character draws at roughly 1.2-1.7 ms and caster selection at 0.8-1.1 ms inside local shadows. Use those costs to select a substantial workload for the first independent recording jobs. Separate shadow/caster algorithm optimizations remain order 2 and wait behind parallel command recording. Keep all required casters, animation and foliage shadows.

Tested an opt-in `-fg_submit_batches` implementation with at most two extra graphics submissions after complete sun/local-shadow passes. It reopened the NVRHI command list, re-uploaded shared volatile frame constants, preserved queue order and left GC at frame end. It did not add parallel CPU recording. The candidate and following off run used the identical executable; all three runs had identical initial profiles, AA/FG off and the same cameras. Every sampled presentation mode was Composed: Flip.

| Scene | Initial off FPS / p99 ms | Submission on FPS / p99 ms | Same-executable off FPS / p99 ms |
|---|---:|---:|---:|
| Interior | 49.28 / 49.49 | 38.89 / 57.33 | 61.03 / 31.10 |
| Outdoor | 53.33 / 45.49 | 43.24 / 49.80 | 67.18 / 25.34 |
| Rain | 61.72 / 35.74 | 42.29 / 61.63 | 69.85 / 24.61 |
| Night | 78.98 / 31.05 | 57.18 / 32.68 | 81.92 / 23.24 |

Evidence names in order: `DX12_COMMAND_CPU_BASELINE_041_B`, `DX12_COMMAND_SUBMIT_041_A`, `DX12_COMMAND_SUBMIT_OFF_041_B`. Each completed four scenes and exited 0, with no logged fatal/device-removal/NVRHI error or omitted shadow lights. All four candidate screenshots were inspected, with no obvious lighting/shadow regression in those views. Existing shader compilation failures remain; this is limited runtime evidence.

**Decision: rejected and removed the submission experiment.** It was slower than both off runs in every scene. The off runs themselves varied substantially, so these data do not isolate an exact causal slowdown. Retained CPU tracing in the maintained source. Normal 0.41 staging executable, PDB and play profile were restored and hash-verified; the delivered package and DX9 baseline remain unchanged. No new FPS improvement is claimed, and these diagnostic runs do not replace the published DX9 comparison.

The workspace summary is `outputs/implementation-evidence/MISERY_DX12_COMMAND_SUBMISSION_SUMMARY.json`; each run retains source patches, executable identity, profiles, captures, logs and timings. First attempt `DX12_COMMAND_CPU_BASELINE_041_A` failed before launch because the staging controller was missing; copying the already verified controller fixed that preparation error.

## Engineering references and how they apply

- Microsoft's [command recording and allocator guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) explains allocator pooling and fence-controlled reuse. Its [queue synchronization guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists) explains cross-queue ownership. Apply these requirements to command lists, temporary resources and worker contexts before enabling concurrent recording.
- NVIDIA's [Command Buffers guide](https://developer.nvidia.com/blog/advanced-api-performance-command-buffers/) recommends balanced parallel recording, sufficient work per list, timely submission and GPU-generated indirect work. Our inference: the sequential recording/end-of-frame submission path is a concrete modernization target. The guide's numerical submission targets are starting points, not fixed settings to copy into this engine.
- NVIDIA's [Async Compute and Overlap guide](https://developer.nvidia.com/blog/advanced-api-performance-async-compute-and-overlap/) selects overlap candidates from unit throughput, resource use and measured idle periods. Use it when restoring DX12 async compute. A queue's existence is not evidence of overlap or a speedup; concurrent workloads can compete for the same resources.
- AMD's [RDNA Performance Guide](https://gpuopen.com/learn/rdna-performance-guide/) covers command allocator ownership, batching, PSO creation, barrier reduction, suballocation and memory budgets. Its recommendation to minimize submissions differs in emphasis from NVIDIA's warning about submitting everything at frame end. Resolve that tradeoff with the actual RTX 3090 timeline. Treat AMD-specific numerical and shader recommendations as architecture-specific.
- NVIDIA's [Ampere GA102 Architecture whitepaper, V1.0](https://www.nvidia.com/content/dam/en-zz/Solutions/geforce/ampere/pdf/NVIDIA-ampere-GA102-GPU-Architecture-Whitepaper-V1.pdf), especially printed pages 9-12, describes the RTX 3090 family's shader instruction paths and shared memory/cache resources. It supports examining instruction mix and bandwidth in expensive vegetation/shadow shaders. Its hardware throughput comparisons are not predictions for this port. Preserve numerical precision unless a measured shader change remains visually correct.

## Historical first implementation checkpoint

Priority 1 remains unfinished: implement independently owned command lists and recording contexts, record substantial jobs on CPU workers, and submit in dependency order. Use the retained CPU trace to choose the first bounded workload; inspect shared `RenderContext`, pass state, upload buffers, profiler state and resource tracking before moving its callback onto a worker. The current backend's plural `ExecuteCommandLists` helper loops over individual submissions (`Backend/D3D12Backend.cpp:687`); true batch submission belongs in the integrated path, but changing an unused helper alone is not a performance result.

Keep the same native-resolution visual preset, AA/FG settings and presentation conditions for before/after measurements. Report CPU/GPU timings, average and p99 intervals, stalls and relevant memory pressure. Keep the fixed DX9 baseline and the 0.41 package available. Recheck moving/offscreen shadows and MISERY behavior affected by each change. The previous rain/night sequences varied too much to support small-effect attribution, so repeat those conditions when they matter to a candidate. Update the comparison report after a measured visual or performance milestone.

No fixed multiplier or universal crash-free result is claimed. Acceptance is a measured improvement with the required visuals and gameplay intact.
