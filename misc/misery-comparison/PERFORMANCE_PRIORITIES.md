# Engine performance and architecture priorities

## Current direction: pursue the largest measured gains (2026-09-09 UTC)

The user's current clarification explicitly includes A-life, AI, physics and streaming taking advantage of x64 memory and safe multithreading. Examine actual execution and capacity, rather than relying on architecture labels or enabled settings. Choose changes by their expected reduction in frame time and stalls, with implementation cost and regression risk considered. Preserve visual quality, complete MISERY behavior, normal simulation responsiveness, saves and reliability. Do not reopen the old priority-order discussion after compaction.

The latest same-executable partitioning comparison, BD/BE, reduced renderer CPU time by 0.571-0.709 ms (11-12.5%), but native application FPS gains were mixed. That is useful CPU progress, not an 11-12.5% whole-game speedup. GPU utilization averaged about 95-97% in BE; these earlier findings informed the delivered graphics optimizations. The current engine-wide work and recurring main-thread stall are prioritized below. GPU utilization alone does not identify an internal hardware bottleneck.

Cumulative progress is substantial: the published 0.40 to delivered 0.43 recordings show interior 57.63 to 101.81 FPS, outdoor 55.94 to 93.47, rain 52.24 to 97.59, and night 58.33 to 113.11, with frame generation off. Application p99 intervals improve from roughly 25-27 ms to 13-15 ms. These historical captures differ in workload and measurement conditions and cannot isolate one optimization's contribution. Mixed gains from the latest recording experiment should not be presented as lack of overall project progress. Source reports: workspace `outputs/MISERY_DX9_DX12_BASELINE_040.json` and `outputs/MISERY_DX9_DX12_COMPARISON_043.json`.

BE uses native 3440x1440, FXAA, AO-high, 16x filtering, conventional and grass shadows, and frame generation off. Existing trace means:

| Scene | Local shadows GPU ms | Sun shadows GPU ms | Foliage drawing GPU ms | Renderer CPU ms | Application interval ms |
|---|---:|---:|---:|---:|---:|
| Interior | 3.597 | 1.683 | 0.047 | 5.309 | 10.300 |
| Outdoor | 2.887 | 1.762 | 1.494 | 4.752 | 11.237 |
| Rain | 2.832 | 1.761 | 1.335 | 4.676 | 10.680 |
| Night | 3.283 | inactive | 1.419 | 4.527 | 9.196 |

Local GPU time sums the two sequential partition ranges. Foliage uses `DetailDraw` only; `Details.Draw` is nested and must not be added again. These are recorded pass costs, not a complete GPU frame breakdown or promises of recoverable savings. Renderer CPU excludes other game-thread work. One interior slow frame in each run spent 38-43 ms in the game-update bucket; its underlying cause and recurrence remain unproven.

Current work order:

1. Independent 0.46 retains the A-life budget correction and reduces the measured 52-object removal notification batch from 35.149 to 15.240 ms while preserving callbacks and order. The remaining removal cost stays a follow-up candidate. Next inspect AI pathfinding's per-search scratch state and existing scheduling, then use existing profiling on representative callers before enabling concurrent searches. Preserve simulation cadence and inspect actual capacity pressure before widening formats.
2. Enable useful independent calculations through the existing task pool with explicit ownership, scratch storage and completion dependencies. A-life switching, object creation/removal, Lua state and save operations share mutable state; investigate their callers before permitting simultaneous mutation. Preserve the existing first-load/precache behavior and normal gameplay. No new simulation concurrency implementation has been tested yet.
3. Retain delivered grouped local-shadow copies and the optional parallel recorder. The next shader experiment is deferred following the user's clarification, not queued for an automatic benchmark. Earlier argument batching remains opt-in and the rejected normal-cache/opaque-sun work remains archived. Return to graphics when its measured benefit warrants it, maintaining DX9 visual and configured-performance comparisons after milestones.

Use the existing captures and profiling tools. Before accepting a change, compare native frame times, tail latency and affected visuals with settings disclosed, and use focused runtime checks for affected resource lifetimes or gameplay. Update the DX9/DX12 comparison after a measured milestone. Reliability defects that block the intended experience continue to take precedence.

The unfinished cost-based recording splitter is preserved, unbuilt and untested, in workspace `work/runtime/deferred-recording-balance-043` (raw sources, parent and SHA-verified patch). The working source has returned to the tested recorder at `6ce2a3a36d1edc07113c6045b516141639dc6102`. No executable, settings, package or published benchmark changed during this reprioritization.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_RECORD_PARTITIONS_043_SUMMARY.json` and BD/BE `partition-analysis.json`. [Microsoft's CPU/GPU bottleneck explanation](https://devblogs.microsoft.com/directx/cpu-and-gpu-boundedness/) supports choosing work according to the limiting processor. [NVIDIA's command-buffer guide](https://developer.nvidia.com/blog/advanced-api-performance-command-buffers/) supports parallel recording while accounting for command-list overhead, GPU idle time and pipeline drains from frequently mixed copy/dispatch/draw work. The suggested local-shadow opportunity is a source-based hypothesis until measured.

## Current engine memory and threading findings (2026-09-09 UTC)

Focused inspection of source at `8725d54bb8ee5648e21ceb05e769cb8fa9c86a91`, the normal 0.44 profile, the completed BW control log and existing memory proof. This does not establish that all legacy limits have been found or removed.

| Area | Verified state | Remaining work / limit |
|---|---|---|
| Native memory / A-life containers | A-life's object registry uses `xr_map`, backed by `xalloc` and native `size_t` allocation. The earlier engine capacity check wrote and read back 4,362,076,160 bytes and freed the allocation. | This proves allocator capacity in that earlier check, not campaign-scale memory behavior. Physical RAM, commit and individual formats still constrain use. |
| A-life updates | Normal 0.44 has `mt_alife on`. `CALifeUpdateManager` queues its update into `Device.seqParallel` after the first update. | `CRenderDevice::ProcessFrame` executes that sequence and `seqFrameMT` in one task alongside rendering. A-life switch and scheduled-object loops remain sequential. This is coarse overlap, not per-object multicore scaling. |
| AI / pathfinding | Path builders and stalker object handlers can join the same sequence; their normal settings are on. | Both stalker and monster vision use `if (false && g_mt_config.test(mtAiVision))`; the parallel branch is disabled despite `mt_ai_vision on`. Stalker blame attributes that branch to upstream commit c96c2e4cc9 (2017), not this DX12 work. Do not simply remove the guard without understanding dependencies. |
| Physics | Physics registration can select `seqFrameMT`, but default device flags include sound/network and omit `mtPhysics`; the normal profile has no physics override. | The usual source-selected path runs physics on the frame sequence. Selecting the other sequence would still provide coarse overlap; it would not parallelize individual physics islands. No runtime physics-thread trace collected in this inspection. |
| Lua / scripts | Current generated build uses static LuaJIT without `nogc64`. Its standalone interpreter reports x64, 8-byte pointers and `gc64=true`. Lua allocations enter the engine through size_t parameters. | Interpreter/build evidence, not an in-game large-script-heap stress test or proof of concurrent use of one Lua state. Script/world-state coordination remains necessary. |
| Resource loading | AsyncIOManager starts four workers in BW and accepts 64-bit offsets/sizes in source. | That manager reports zero completed requests and zero bytes read in BW. Worker creation alone does not prove useful streaming concurrency; inspect the actual texture/file loading callers. Other I/O paths were not exhaustively reviewed. |
| Fixed capacities / formats | ALife object IDs and global graph IDs are still u16. The task allocator/queue uses 4,096 entries per worker. Normal MISERY config retains five scheduled objects per update and its authored scheduling values. | Identifier formats, queue capacity and simulation budgets are different constraints from the process address space. No current object-ID or task-queue exhaustion is established. Changes need relevant workload evidence and save/graph compatibility. |

A-life issue identified before the correction below: `set_process_time` computed `microseconds - microseconds * factor / 1,000,000`, then passes that value unchanged to a timer measured in seconds. At the installed values 10,000 and 0.75, single-precision evaluation gives about **9,999.992 seconds**; converting the complete remaining budget gives **0.0025 seconds**. This defeats the apparent intended switching time budget. The iterator does restart its timer on every update; first-load/precache bypass behavior also exists. The correction and bounded runtime observations are recorded below; no per-object concurrency or full-campaign validation is claimed.

Source pointers: [A-life update and budget](../../src/xrGame/alife_update_manager.cpp), [sequential worker execution](../../src/xrEngine/device.cpp), [scheduled-object loop](../../src/xrGame/alife_schedule_registry.h), [safe iterator and timer checks](../../src/xrGame/safe_map_iterator_inline.h), [graph budget forwarding](../../src/xrGame/alife_graph_registry_inline.h), [stalker vision](../../src/xrGame/ai/stalker/ai_stalker.cpp), [monster vision](../../src/xrGame/CustomMonster.cpp), [physics registration](../../src/xrPhysics/PHWorld.cpp), [native allocator](../../src/xrCore/Memory/xalloc.h), [Lua build](../../Externals/LuaJIT-proj/CMakeLists.txt), [I/O workers](../../src/Layers/xrRender/ResourceManager/AsyncIO.cpp), [object IDs](../../src/xrServerEntities/alife_space.h), [graph IDs](../../src/xrAICore/Navigation/game_graph_space.h), [task pool](../../src/xrCore/Threading/TaskManager.cpp).

Workspace evidence: `outputs/implementation-evidence/NATIVE_X64_MEMORY_CAPACITY.log` and its result JSON; `LUAJIT_X64_ARCHITECTURE_044.json`; `DX12_PBR_LIGHT_CULL_OFF_044_BW/engine.log`, profile, traces and `control-analysis.json`. BW's six in-window slow-frame records show game-update costs of 1.907-10.431 ms, and zero additional wait after rendering for the shared worker sequence. These are selected slow frames, not a full A-life timing breakdown or evidence that the worker's total cost was zero.

The PBR zero-contribution shader edit is preserved in [an untested patch](pbr-light-cull-044-deferred.patch). BW tested the unmodified shader with the existing 0.44 executable; BX was never launched after the user clarified engine-wide scope. No candidate shader was staged or performance benefit measured. Both game and PresentMon exited0; runner43229 and sampler34122 are terminal0. All four control stills were opened; geometry/lighting/rain are intact, with night darkness limiting subtle assessment. Protected normal0.41 EXE/PDB/profile and the original isolated shader cache are restored. Delivered0.44, fixed DX9 and prior packages retained. The shader cache's missing include dependency hashing was handled by a fresh isolated cache for BW, not fixed in production. Do not resume this benchmark automatically.

## Object-removal dispatch and 0.46 delivery (2026-09-09 UTC)

Code commit `36213bc8e2861271c84a2a0938ef78fcc4f41e87` is pushed. Release builds force `PURE_DYNAMIC_CAST`, so `CScriptBinder::net_Relcase` previously performed full RTTI for every receiver/removed-object pair before checking whether the receiver had a binder. The change returns immediately for absent binders and adds a typed virtual query to `IGameObject`, overridden by `CGameObject`. Native notification loops, Lua callback selection/order, reference cleanup, scheduling, identifiers and serialization remain. All dependent native modules were rebuilt together. `-relcase_legacy_cast` retains the previous cast-first behavior for same-binary controls; `-relcase_validate` compares results against RTTI. `-relcase_trace` exposes sub-8 ms removal stages in the existing ObjectListTrace logger. All are off by default.

The final native DX12 probe CE passed inherited default, class override/replacement, instance override/removal and restored default cases. Its direct TSV contains six passes and completion; the optional Lua printf does not appear in the engine log. At least 3,342,336 type-query checks agreed with RTTI across loading, the probe and shutdown; 934,088 logged checks had no binder. No null or non-game-object inputs occurred. Game/PresentMon/runner 41244 exited 0, with no matched fatal/native-debug/NVRHI error. Existing native warnings 679/820/821 and 836 legacy shader failures remain. The reused probe is now versioned as `dx12_relcase_probe.script`.

CF (legacy) / CG (optimized) used the same executable, profile, controller, authored A-life config and camera replays, native 3440x1440 FXAA/AO3/16x, conventional/grass shadows, SDR, FG off, serial recording and grouped copies. Both enabled CPU, slow-frame and removal-stage logs. Fourteen GPU samples per scene had zero invalid counters; sampled background engines stayed below 0.84%. Cameras, weather names and recorded clocks match.

| Measurement | Legacy CF | Optimized CG |
|---|---:|---:|
| Active / sleeping receivers in 52-object batch | 862 / 2614 | 875 / 2557 |
| Estimated notifications | 180752 | 178464 |
| Active removal notifications ms | 15.625 | 8.283 |
| Sleeping removal notifications ms | 19.524 | 6.957 |
| Combined notification time ms | 35.149 | 15.240 |
| Corresponding full frame / FrameMove ms | 47.775 / 40.553 | 28.880 / 20.759 |

This is about 57% less notification time with about 1.3% fewer receivers. It is one observed batch per mode, not a universal gain. The legacy batch at time 37341 falls inside the trimmed interior window; the optimized batch at 35830 precedes its window. Do not use scene p99/max differences to quantify the batch change. Overall application FPS/p99 are mixed:

| Scene | FPS legacy / optimized | p99 ms legacy / optimized |
|---|---:|---:|
| Interior | 102.43 / 101.32 | 13.76 / 14.93 |
| Outdoor | 88.43 / 90.14 | 16.07 / 15.13 |
| Rain | 93.16 / 92.84 | 15.14 / 15.31 |
| Night | 96.18 / 94.55 | 14.60 / 15.43 |

CF runner 68335/sampler 93411 and CG runner 60733/sampler 53531 reached terminal 0; both game/PresentMon exits are 0. All eight stills were opened; surfaces/NPC/floor lighting, geometry/foliage, rain and night silhouettes show no obvious new corruption. HUD/weather/wind vary, and dark night limits subtle assessment. No temporal/HDR proof. Existing 836 legacy shader failures remain in each; no matched fatal/device error.

The initial build compiled, but the first logging edit mistakenly landed in SingleUpdate. CC's six-case behavior probe passed, then partial timing run CD was stopped (game -1; runner 45864/sampler 76050 terminal 0) and excluded. The logging placement was corrected and the new executable was retested as CE/CF/CG. Preserve the failed-attempt artifacts under workspace `work/runtime/relcase-cast-046/initial-logger`; do not treat CD as a completed benchmark.

Private package `D:/Codex/MISERY-DX12/outputs/MISERY_DX12_DEV_0.46` contains the tested EXE `ec5a100f21210c04416e5b487df3b9de1cbe51ad9225d1a9c1bc479585b127e2` and PDB `7fc27eddcf1f04331d73158c6f5b7ba8130da412efd2f09d218fa01ee1326d14`. Compiled parent `cb92d5a3f607f67560893f50e69df7973d7c844a`, patch SHA256 `3192714c5688f752143dd36e6c5f8945fe63748cb7f7661efb9315a4cbbb9b76`, raw sources and dependency/build identity are archived. NVRHI remains `dcf5f012187e9482d99b71d224ba09304cffb35e`; LuaJIT remains `5a5cd82e435a7b08d61b4e0af887493d24d9eca1`. Packaging session 76699 exited 0; all 17,170 game files (11,454,052,963 bytes) initially matched staging by SHA256.

Actual-package CH ran normal serial recording with internal traces off: FPS 103.94 / 92.12 / 97.20 / 100.92; p99 ms 13.54 / 15.15 / 14.35 / 14.21. This is one run, not a matched prior-package comparison. Game 31088, PresentMon 9952, runner 72897 and sampler 5179 all exited 0. Fourteen GPU samples per scene, zero invalid counters, sampled background below 0.69%; no continuous CPU-contention trace. All four actual-package stills were opened, for 12 inspected final-build images total. No matched fatal/device error; 836 legacy shader failures remain. The normal package profile was restored and 14 critical files rehashed against the manifest, including both save pairs and replays. Protected 0.41 workspace EXE/PDB/profile and the old cleanup-probe TSV were restored separately. Previous packages and the fixed DX9 reference are preserved.

Report: workspace `outputs/MISERY_DX9_DX12_COMPARISON_046.html`. Evidence: `MISERY_DX12_RELCASE_CAST_046_SUMMARY.json`, `MISERY_DX12_PACKAGE_046_SUMMARY.json`, CE `probe-analysis.json`, CF/CG `removal-analysis.json`, and CH `package-analysis.json` under `outputs/implementation-evidence`. All 11 cited graphics/reference-source files remain hash-identical to 0.45; no graphics-quality reduction. No new in-process reload, broad transition, campaign/effects parity or extended-session certification is claimed for 0.46; earlier 0.45 reload coverage remains separate. Broader simulation concurrency is still unfinished.

## A-life switching budget correction (2026-09-09 UTC)

Retain the correction: convert the complete remaining process budget from microseconds to seconds. Authored process_time=10000 and monster factor=0.75 now provide a 2.5 ms switching budget; the old expression effectively allowed 9999.992 seconds. The existing iterator, continuation, first-load/precache bypass, all objects/callbacks and five scheduled objects per update remain. Optional `-alife_trace` records checks, times, worker ID and sampled visit cycles; `-alife_legacy_budget` reproduces old arithmetic in the same executable. Diagnostics are absent from the normal launcher. This is budgeted sequential work alongside rendering, not new per-object threading.

Configure and Release build passed. Parent 8f0ac7e1b9a3eac395c0f03dc3c4359a3615d893, tested patch 2c26e5d2ad8b513f75000d3546707b45be156e0c8f583a8edf3c48433abe10ca, EXE d8f81bb7eb203a969e850da4aabe1f072b127ec0680921bf5954d111ec52f8e0, matching PDB 3c39345b77cd65305c16058e6b1562a1407947ac0f9d7bf978c134c379333025. Raw source/build identity and binaries: workspace work/runtime/alife-budget-045. Pinned NVRHI dcf5f012187e9482d99b71d224ba09304cffb35e and LuaJIT 5a5cd82e435a7b08d61b4e0af887493d24d9eca1 are unchanged. The initial successful build was superseded before runtime tests by corrected diagnostic handling of the u64(-1) unvisited sentinel.

| Scene | Switch ms old / fixed | Total A-life ms old / fixed | Application FPS old / fixed | p99 interval ms old / fixed |
|---|---:|---:|---:|---:|
| Interior | 3.080 / 2.623 | 3.633 / 3.258 | 95.80 / 94.09 | 14.76 / 15.47 |
| Outdoor | 3.238 / 2.552 | 3.818 / 3.135 | 83.50 / 81.94 | 16.25 / 16.93 |
| Rain | 3.184 / 2.636 | 3.739 / 3.215 | 86.50 / 85.45 | 15.84 / 15.71 |
| Night | 3.269 / 2.722 | 4.206 / 3.677 | 99.83 / 106.09 | 14.37 / 13.57 |

BZ/CA are one sequential same-executable pair at native 3440x1440, FXAA/high AO/16x, conventional and grass shadows, serial recording, grouped shadow copies and FG off. Both use A-life and CPU/GPU/slow-frame traces; only BZ adds the legacy-budget flag. Starting profiles, authored A-life config, controller and camera replays match; camera values match within 0.001. Interior clocks differ by one minute. All four windows contain 14 GPU samples with no invalid counters; background engines stay below 0.79%. Workloads vary (for example night mean lights 43.03/45.17). Preflight CPU is not continuous contention measurement. Application FPS/tails are mixed; do not claim a general frame-rate improvement. The reduction is less switching work per invocation because it now yields at the intended budget, not faster per-object calculations.

CA logged 739 A-life updates. In all measured normal windows both registries have zero sampled unvisited objects and their oldest counters advance. The fixed switching loop yields in 425 of 429 measured updates, with sampled oldest/newest switch age at most one update cycle during daytime and two at night. Scheduled work remains five objects per update. The timer checks between objects; a callback or OS scheduling can exceed 2.5ms, and scheduled-object costs are additional. This is bounded continuity evidence, not all-level/campaign or long-session proof.

BY separately forced a 25 us switching budget and passed two complete Zaton unload/reloads with native DX12 validation, partitioned recording, DLSS Quality and FSR FG. Three captures retain 153 inventory contents/condition/ammo entries and 22,927 Grouse bytes; raw bolt IDs differ. Last successful FG dispatch count 1716. BY's 12-second phases were too short for complete scheduled-object coverage; normal BZ/CA supplied the longer continuity observation. Native warnings 679/820/821 remain, with 2216 legacy shader failures  in BY and 836 each in BZ/CA. No matched fatal/native/NVRHI error. All game/PresentMon/runner/sampler handles reached terminal 0.

All eleven actual screenshots were opened: three reload views and eight comparison stills. NPC/counter/fence/floor lighting, bridge/terrain/foliage, rain/fog and night silhouettes remain intact without obvious new corruption. HUD/weather/wind patterns differ, and dark night images limit subtle assessment. No temporal/HDR or complete effects claim. Fourteen BY probes were archived/hash-verified/removed and the original control/authored A-life config restored.

A separate legacy restriction is now explained: xrSheduler.cpp:377-381 maps authored schedule_min/max=1 and A-life scale 0.5 to a 265 ms target; measured means are 268-274 ms. That cadence remains unchanged. The remaining repeated interior slow frame spends 44.784 ms (BZ) / 41.173 ms (CA) in FrameMove, with zero extra worker wait. The adjacent ObjectListTrace already identifies release_active/release_sleeping as 13.027+25.345=38.372 ms in BZ and 15.207+19.733=34.940 ms in CA. Each destroys 52 objects. xr_object_list.cpp:347-355 broadcasts each removal to all active/sleeping objects, yielding 179,920 and 181,116 net_Relcase calls at the recorded counts. Investigate these implementations and reference ownership next; do not skip callbacks or blindly parallelize shared Lua/world mutation. No object-removal optimization is implemented yet. Workspace evidence: outputs/implementation-evidence/MISERY_DX12_ALIFE_BUDGET_045_SUMMARY.json, BY reload-analysis.json and BZ/CA alife-analysis.json. Independent 0.45 is delivered at D:/Codex/MISERY-DX12/outputs/MISERY_DX12_DEV_0.45. All 17,170 game files (11,453,935,203 bytes) matched staging by SHA256. CB tested the actual package with all A-life/CPU/GPU/slow-frame/workload diagnostics off and normal serial recording: FPS 97.90/86.45/91.42/93.05; p99 ms 14.18/16.01/15.91/15.14. This is not a matched 0.44/0.45 package pair. Four package stills were opened, bringing this milestone to 15 inspected images. All four scenes completed with game/PresentMon/runner 10578/sampler 51657 exit 0 and 836 existing legacy shader failures, no matched fatal/device error. Fourteen GPU samples per scene, no invalid counters, background engines below 0.72%; CPU preflight 22% is not continuous contention evidence. The package play profile was restored and 17 critical manifest files rehashed; protected 0.41 staging EXE/PDB/profile restored separately. Previous packages and fixed DX9 are preserved. Source f4a98b7d02dcc830bac84637f316610f2e76c0a0 is pushed. Current report: outputs/MISERY_DX9_DX12_COMPARISON_045.html; actual package evidence: outputs/implementation-evidence/MISERY_DX12_PACKAGE_045_SUMMARY.json.

## Earlier experiment: cached detail-model normals (2026-09-09 UTC)

Rejected for promotion. Clear-weather foliage GPU time improves only 0.031 ms, rain worsens 0.020 ms, and nighttime results are confounded by different lighting work. No consistent affected-pass or application benefit. Five production source changes restored to parent b24295865; exact patch/binaries retained. Delivered 0.44 remains normal.

The archived `-detail_cached_normals` candidate adds a separate vertex shader and immutable `StructuredBuffer<float3>` at free SRV slot42. It computes the original triangle cross product, normalization and 0.001 degeneracy threshold once at model load; rotation and height-dependent wind bending remain per vertex. Original position/UV/index buffers, instance generation, culling, pixel lighting, motion and shadows retain behavior. OFF keeps the original shader and allocates no normal buffer. No production source/default/package promotion.

| Scene | Foliage GPU ms OFF / ON | Application FPS OFF / ON | p99 ms OFF / ON |
|---|---:|---:|---:|
| Interior | 0.046 / 0.047 | 96.89 / 93.36 | 14.02 / 15.21 |
| Outdoor | 1.541 / 1.510 | 83.75 / 83.08 | 16.36 / 16.52 |
| Rain | 1.584 / 1.603 | 84.36 / 84.79 | 16.13 / 15.69 |
| Night | 1.614 / 2.127 | 99.56 / 86.28 | 14.63 / 16.10 |

One sequential same-executable pair with matching starting profiles, controller, replays, cameras and game clocks. Native 3440x1440, FXAA/high AO/16x, conventional and grass shadows, serial recording, grouped local-shadow copies ON, FG OFF; CPU/GPU/slow-frame tracing in both. The only added command-line flag is -detail_cached_normals. OFF uses the original vertex shader and creates no normal buffer; ON uses the separate cached variant. Night mean lights differ 43.10/50.21 and local-shadow GPU time 2.485/3.309 ms; daytime light/object workloads also vary. This prevents attributing the nighttime slowdown solely to the cache. There are 13-14 GPU samples per scene, sampled background engines below 0.56 percent and no invalid in-window GPU counters. Preflight CPU snapshots are not continuous contention measurement. PresentMon measures application API intervals, not displayed frames, latency or drops.

Configure/build exit0. Compiled parent `b242958652f6bc9afb285a2beef9404426e6c7f3`, patch SHA256 `58d5e8e0add679369351a286bc27c710263c3054de96ec2803547aaf06c02e36`, executable `ab49a5bac413e768a191acec83c22f859b6c845244adae552319254302686c1f`, PDB `203c3f99561b241c158fe3d61eb55fcff2e4e74abcec648d4e2ea8c92bbd96f2`. NVRHI pin remains `dcf5f012187e9482d99b71d224ba09304cffb35e`. Exact sources and binaries are in workspace `work/runtime/detail-normals-044`; [replayable patch](detail-normals-044.patch) passes `git apply --check` against the restored parent source.

BT passed two full unload/reloads with native validation, parallel partitions, DLSS Quality and FSR FG. All three loads cached37 models,356 triangles,2,664 entries and31,968 bytes, with zero degenerate triangles; the cached vertex shader loaded. All153 inventory contents/condition/ammo entries and22,927 Grouse bytes match; raw bolt IDs differ. Final successful FG dispatch1450. Native warnings679/820/821 and2,216 legacy shader failures remain. BU/BV each retain836 legacy shader failures. No matched new fatal/native/NVRHI error; all game/PresentMon/sampler processes exit0.

All eleven stills opened: three BT reload images and all eight BU/BV comparison images. Interior NPC/counter/fence/floor lighting and shadow edges, outdoor terrain/bridge/foliage cutouts, rain/fog and night silhouettes remain consistent without obvious new corruption. Vegetation movement and rain/leaf patterns differ; very dark night images limit subtle shading assessment. No exact numerical normal-buffer equality, temporal FG/HDR or full effects-parity claim.

Fourteen BT probe files archived/hash-verified/removed, original control restored byte-exact. Protected normal0.41 EXE/PDB/profile and original billboard shader restored/rehashed; only the newly staged cached wrapper was removed after checking its hash. Delivered0.44, previous packages and fixed DX9 reference retained. Existing comparison report #detail-normal-experiment contains the new four sliders; prior data preserved. Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_DETAIL_NORMALS_044_SUMMARY.json`, BT `reload-analysis.json` and BU/BV `normal-analysis.json`.

[Microsoft's shader guidance](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-optimize) supports removing unnecessary work and skipping zero-contribution lighting. This is a hypothesis-selection guide, not hardware performance proof. Next inspection found `shared/clustered_lighting.h` samples local shadows before PBR evaluation, while `shared/pbr_brdf.h` multiplies the entire PBR result by nonnegative NdotL. Preserve authored-material lighting when investigating that opportunity; no change has been made yet.

## Earlier experiment: opaque sun-shadow depth (2026-09-09 UTC)

Rejected for promotion: skipping the pixel shader on opaque world/terrain sun-shadow casters did not reduce the affected GPU pass. The three production C++ files were restored to parent `1488eb851b7c59507449eb41d1dbdeabffb61bf0`. The exact tested change remains in [sun-opaque-depth-044.patch](sun-opaque-depth-044.patch); `git apply --check` passed against that restored source. Delivered 0.44 remains normal. Do not repeat these checks without a concrete new hypothesis.

The archived candidate's `-sun_shadow_opaque_depth` uses the actual CPU material alpha-test flags to stable-partition the existing visible command list. Opaque ranges use a null pixel shader; cutouts keep `sun_shadow.ps`. One upload serves the two ranges, with unchanged vertex/raster/depth state, resolution, bias and caster coverage. Skinned/detail and local-shadow paths retain their original behavior. A per-frame assertion checks the combined world-caster total.

Configure/build exited 0. Compiled source is parent `1488eb851` plus patch SHA-256 `eb0f1a81dbb13193b71e124a803975e6aa9999f974b98bc8b18b8e05c9ef4ff4`. Executable SHA-256 `2f887b00702af3381363aa2d86fe8cfc17527df73ffb61e4880ed97432b808ed`; PDB, exact raw sources and identities are archived in workspace `work/runtime/sun-opaque-depth-044`. NVRHI remains pinned at `dcf5f012187e9482d99b71d224ba09304cffb35e`.

BR/BS use the same executable, native 3440x1440, matching profiles/controller/replays/cameras/game clocks, FXAA/high AO/16x, conventional and grass shadows, serial recording and FG off. Both enable CPU/GPU/slow-frame traces and the delivered grouped local-shadow copies. Only BS adds the new sun flag; earlier indirect argument batching remains OFF.

| Scene | Sun GPU ms OFF / ON | Sun CPU ms OFF / ON | Application FPS OFF / ON | p99 ms OFF / ON |
|---|---:|---:|---:|---:|
| Interior | 1.711 / 1.737 | 1.416 / 1.460 | 89.20 / 92.47 | 18.67 / 16.09 |
| Outdoor | 1.762 / 1.804 | 1.412 / 1.444 | 80.77 / 83.35 | 17.26 / 16.14 |
| Rain | 1.767 / 1.798 | 1.419 / 1.411 | 83.45 / 85.86 | 18.32 / 15.93 |
| Night | 0.012 / 0.012, drawing inactive | 0.005 / 0.005 | 81.80 / 101.65 | 17.66 / 14.22 |

These are one sequential pair with live simulation. The FPS increase cannot be credited to the edit: the largest change is at night while sun-shadow drawing is inactive. Night mean light counts are 51.03/42.52, local-shadow GPU time 3.423/2.432 ms, and foliage time also differs. ON has six more local faces in each daytime scene (207/138/132 versus 201/132/126), one extra light and different HUD/particle work. There are 13-14 GPU samples per scene, sampled background engines below 0.73%, and zero invalid in-window GPU counters. Preflight CPU snapshots are not continuous contention measurement. API-only PresentMon does not establish displayed frames, latency or drops.

BQ completed two full Zaton unload/reloads with native DX12 validation, parallel partitions, DLSS Quality and FSR FG. All 35 sampled opaque/masked sums cover the original world-caster totals. Inventory contents/condition/ammo match across three 153-item captures; raw bolt IDs differ. Grouse's 22,927 authored bytes match; the final successful FG dispatch count is 1409. Existing native warnings 679/820/821 and 2,216 legacy shader failures remain, with no matched fatal/native/NVRHI error. BR/BS each retain 836 legacy shader failures and no matched fatal/device error. All owned games, captures and samplers exited 0.

All eleven stills were opened and inspected: eight BR/BS images plus three BQ reload images. Counter/NPC/fence/floor shadow edges, terrain/bridge/foliage cutouts and rain/fog remain consistent without obvious new corruption. Dialogue text, wind and rain streaks differ. Very dark night images limit subtle shadow assessment. Stills do not establish moving-shadow, temporal FG, HDR or complete effects parity. Fourteen unique BQ probes were archived/hash-verified/removed, original control restored byte-exact, and protected normal 0.41 EXE/PDB/profile restored and rehashed. Delivered 0.44 and the fixed DX9 reference are preserved.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_SUN_OPAQUE_044_SUMMARY.json`, BQ `reload-analysis.json`, BR/BS `sun-opaque-analysis.json`, and existing report `outputs/MISERY_DX9_DX12_COMPARISON_044.html#sun-opaque-experiment` with four separate sliders. Earlier delivered-build and experiment data remain unchanged.

[NVIDIA's PSO guide](https://developer.nvidia.com/blog/advanced-api-performance-pipeline-state-objects/) supports grouping draws by pipeline state. [GPU Gems' pipeline discussion](https://developer.nvidia.com/gpugems/gpugems/part-v-performance-and-practicalities/chapter-28-graphics-pipeline-performance) motivates eliminating fragment work in opaque depth passes. This older general advice informed the hypothesis; it did not establish a gain on the RTX 3090, and the measured result does not support promotion.

## Earlier experiment: shadow indirect argument uploads (2026-09-09 UTC)

Optional `-local_shadow_batch_args` collects dynamic-rigid and animated-tree indirect commands for all owned local-shadow faces before drawing. Each nonempty group uploads once into a partition-owned buffer, and faces draw their original ordered ranges at byte offsets. Cold/moved-light static-cache refreshes retain separate scratch buffers. Original frustum/tree/light-sphere predicates, shaders, draw ordering, skinned/detail draws and quality settings remain intact. `-shadow_args_validate` reconstructs the original command sequence and compares its bytes, including empty selections. The normal launcher does not enable this experiment.

Configure/build exited 0. Exact tested source is parent `048bfe8ea1a7d8bf441faf2f51757e9e08a91144` plus patch SHA-256 `82964a89871318c8c52a439926594daaff667d1f072b3037ad2889981d75e5b4`. Executable SHA-256 `6e5a474ca03fdeebda951e1da3849cf9114436d20e42e44e1f7223d8b32b822a`; matching PDB and raw source identities are archived in workspace `work/runtime/shadow-args-044`. NVRHI remains pinned at `dcf5f012187e9482d99b71d224ba09304cffb35e`.

BO/BP compare this same executable with the new switch OFF/ON. Both keep `-local_shadow_batch_copies`, serial recording, native 3440x1440, FXAA, AO-high, 16x filtering, conventional/grass shadows and FG off. CPU/GPU/slow-frame tracing is enabled in both; starting profile/controller/replays/cameras and game clocks match.

| Scene | Local CPU ms OFF / ON | Local GPU ms OFF / ON | FPS OFF / ON | Application p99 ms OFF / ON |
|---|---:|---:|---:|---:|
| Interior | 2.733 / 2.573 | 2.484 / 2.070 | 90.23 / 96.18 | 20.57 / 14.56 |
| Outdoor | 2.350 / 2.235 | 2.323 / 2.383 | 82.76 / 83.64 | 17.32 / 16.17 |
| Rain | 2.346 / 2.184 | 2.496 / 2.352 | 77.35 / 88.28 | 18.05 / 16.72 |
| Night | 2.888 / 3.087 | 2.917 / 2.991 | 90.12 / 89.46 | 15.45 / 16.58 |

Retain as experimental and off by default. Daytime local CPU falls 0.12-0.16 ms, but GPU results are mixed and night worsens. These are one sequential pair, not isolated proof of the full FPS differences. BO has more background GPU activity (up to 2.394% per sampled engine versus 0.839% in BP) and a cluster of interior render/update stalls near 63 seconds. The stall cause is unproven. Night mean lights change 42.79 to 49.10 and spatial objects 468.86 to 488.86, while face means are 141.28/140.41. Other GPU pass costs also vary. BO has 13-14 GPU samples per scene and BP has 14; CPU snapshots do not continuously measure contention. Application API intervals do not establish displayed-frame, latency or drop behavior.

BN completed two full Zaton unload/reloads with native DX12 validation, parallel partitions, DLSS Quality and FSR FG. All 58 active sampled local partitions passed ordered command equivalence; the diagnostic also runs between trace samples. Inventory contents/condition/ammo match across all three 153-item captures; raw bolt IDs change. Grouse's 22,927 authored bytes match exactly. Last successful FG dispatch count is 1783. Animated-tree commands and empty rigid selections were exercised; no warm dynamic-rigid draws were observed in this reload scene. Native warnings 679/820/821 and existing legacy shader failures remain, with no matched fatal/native/NVRHI error.

All eleven new stills were opened and inspected: eight BO/BP comparison images and three BN reload images. Interior NPC/counter/fence/floor lighting and shadow edges, outdoor terrain/vegetation and rain/fog remain without obvious new corruption. Wind/rain patterns and reload clouds differ; very dark night images limit subtle assessment. This is not complete effects parity, moving-shadow or temporal/HDR quality proof. Both games/PresentMon/samplers and the native reload processes exited 0. Fourteen unique reload files were archived, hash verified and removed; control restored byte-for-byte. Protected normal 0.41 workspace EXE/PDB/profile were restored and rehashed. Delivered 0.44 and the fixed DX9 reference remain preserved.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_SHADOW_ARGS_044_SUMMARY.json`, BN `reload-analysis.json` and BO/BP `args-analysis.json`. The existing `outputs/MISERY_DX9_DX12_COMPARISON_044.html#shadow-args-experiment` now includes four separate experimental sliders and the mixed results, while retaining the delivered-build comparison. No new package or default promotion. Next: inspect the remaining sun/foliage costs before implementing another change; do not repeat these completed checks absent new evidence.

## Latest delivery: 0.44 (2026-09-09 UTC)

Delivered private package `D:\Codex\MISERY-DX12\outputs\MISERY_DX12_DEV_0.44` with grouped shadow copies enabled in the normal launcher and serial recording retained. The optional parallel launcher adds both existing parallel/partition flags. All 17,170 initial game files (11,453,828,707 bytes) were copied independently and SHA-verified. The package preserves the exact tested executable `d01c555fb2eb0011d57a6f39bc09d0d03689635d14abf5ef18fefc7de17ff0ea`, matching PDB, compiled parent/patch and committed source identity. No recompilation or renderer change after the BI/BJ/BK checks was required.

Actual package run BL completed all four scenes. Its FPS was below the older AV recording, so a current-condition control BM used copied 0.43 executable/PDB files in the same new runtime directory. All initial game-file hashes match 0.43 except those two binaries. The original 0.43 package was not modified. Current 0.44 binaries and the normal profile were restored and rehashed immediately afterward.

| Scene | 0.43 control FPS | 0.44 package FPS | 0.43 / 0.44 p99 ms |
|---|---:|---:|---:|
| Interior | 88.00 | 97.43 | 16.64 / 14.10 |
| Outdoor | 82.37 | 86.19 | 16.56 / 15.86 |
| Rain | 83.26 | 86.39 | 16.23 / 16.02 |
| Night | 85.45 | 104.55 | 16.46 / 13.39 |

The new build is 3.8-22.4% faster with lower p99 in every scene of this sequential pair. Both retain native 3440x1440, FXAA/AO-high/16x, grass/conventional shadows, FG off and serial recording. Pass timers are off, with one workload log per second. Cameras/lens/profiles/controller match; game clocks differ by up to one minute and simulation continues. Daytime local-face counts match at 201/132/126; night means are about 139. Fourteen GPU samples per scene, background engines below 1%. These are current observations, not a universal gain or a causal explanation for differences from older captures. API-only PresentMon does not establish displayed-frame, latency or drop behavior.

Both games, PresentMon captures and GPU samplers exit 0. All eight BL/BM images were opened and inspected: geometry, NPC/counter lighting, terrain/foliage, rain/fog and dark night silhouettes are consistent. Wind/rain/HUD differences remain; night darkness limits subtle assessment. Each run retains 836 legacy shader failures with no matched fatal/device error. Prior BI native two-reload, DLSS/FG and partitioned-recording checks apply to this identical executable. Full campaign/effects, temporal/HDR quality, broader transitions and extended stability remain unfinished.

Package post-test checks cover presence/size of all 17,170 files and SHA hashes of twelve critical or writable files, including restored executable/PDB/profile, included saves and camera replays. Protected normal 0.41 workspace staging was also restored and hash-verified. The fixed DX9 reference and earlier packages remain available. Updated workspace report `outputs/MISERY_DX9_DX12_COMPARISON_044.html` contains the actual package versus fixed DX9 sliders, refreshed settings and a separate current 0.43/0.44 comparison. Evidence: `outputs/implementation-evidence/MISERY_DX12_PACKAGE_044_SUMMARY.json`.

Next: choose further GPU work from the remaining measured pass costs, retaining the delivered 0.44 baseline. Local shadows still take roughly 2.3-3.3 ms in the earlier traced copy-ON windows; inspect remaining repeated culling/drawing or shader costs before selecting another change. Sun shadows and foliage remain substantial. Do not rerun completed comparisons without a new change, failure or unresolved concern, and do not require a fixed multithreading-first sequence.

## Earlier measured progress: grouped shadow copies (2026-09-09 UTC)

The quieter BJ/BK comparison supports retaining `-local_shadow_batch_copies` for integration into the next development delivery. It reduces repeated copy/draw transitions without removing any caster type or changing shaders, culling, resolution or quality. The archived executable still requires the flag; delivered 0.43 and its normal launch behavior are unchanged at this checkpoint.

| Scene | Local shadow GPU ms OFF / ON | Renderer CPU ms OFF / ON | Application FPS OFF / ON | Application p99 ms OFF / ON |
|---|---:|---:|---:|---:|
| Interior | 3.990 / 2.302 | 6.990 / 6.706 | 83.59 / 96.42 | 16.18 / 14.49 |
| Outdoor | 3.321 / 2.512 | 6.511 / 6.380 | 77.83 / 81.03 | 17.58 / 16.37 |
| Rain | 3.127 / 2.558 | 6.460 / 6.305 | 80.70 / 84.61 | 17.34 / 16.01 |
| Night | 4.086 / 3.263 | 5.687 / 5.594 | 81.65 / 86.15 | 17.47 / 16.42 |

The GPU pass saves 0.57-1.69 ms (18-42%); application FPS improves 4.1-15.3%, and p99 intervals improve 1.06-1.70 ms in these four windows. Both use the same candidate executable, serial recording, native 3440x1440, FXAA/AO-high/16x/conventional and grass shadows, FG off, and matching profiles/controller/camera replays/clocks. CPU/GPU/slow-frame traces are on in both; only BK adds the copy flag. These traced numbers must not replace or be compared directly with the untraced published 0.43 FPS.

The user paused competing work. No busy Python audit workers appeared in the saved CPU snapshots. Each scene has fourteen valid GPU samples; the largest individual background engine stays below 0.73%, and NVIDIA overall GPU use averages 97-99%. This is one sequential pair with small changing light/face workloads, not a continuous CPU-contention trace or a universal/statistically established speedup. PresentMon records application API intervals only, with no displayed-frame, display-latency or drop evidence.

All eight BJ/BK screenshots were opened and visually compared. Interior lighting/shadow boundaries, terrain/foliage, rain/fog and night silhouettes remain consistent; normal animation differs and night darkness limits subtle assessment. Combined with the eleven BI/BG/BH images below, nineteen experiment images have been inspected. BJ/BK game, PresentMon and samplers exit 0; each retains 836 existing legacy shader failures with no matched fatal/device error. BI's native two-reload, inventory/Grouse and partitioned-recording checks below cover the same executable. Complete effects, temporal/HDR quality and extended gameplay remain unfinished.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_LOCAL_COPY_CLEAN_043_SUMMARY.json`; runs `DX12_LOCAL_COPY_CLEAN_OFF_043_BJ` / `DX12_LOCAL_COPY_CLEAN_ON_043_BK`. Reproduce analysis with `python -X utf8 work/runtime/local-shadow-copy-043/compare.py --before DX12_LOCAL_COPY_CLEAN_OFF_043_BJ --after DX12_LOCAL_COPY_CLEAN_ON_043_BK --output MISERY_DX12_LOCAL_COPY_CLEAN_043_SUMMARY.json` (this regenerates measurements and requires restoring the saved review annotations). Normal separate 0.41 staging executable/PDB/profile restored and hash-verified. Next: include the tested batching option in the next development delivery, then select further GPU work by measured cost and expected benefit.

## Historical checkpoints

The dated entries below preserve prior decisions and evidence. Their old next-step and priority labels do not override the current direction above.

## Cached shadow-depth copy experiment (2026-09-09 UTC)

Implemented `-local_shadow_batch_copies`, disabled by default. Warm static depth faces are copied together within their recording partition, with their copy/depth transitions batched through NVRHI's existing state tracking. Invalid caches and moving lights retain the old update/copy path. Dynamic, wind-animated tree, skinned and detail shadows retain their original drawing and culling. An invariant checks that every rendered face receives one cached depth copy. No shaders, quality settings, game callbacks or resource-lifetime rules change.

This tests NVIDIA's command-buffer guidance about avoiding frequent copy/draw/dispatch switches. Source review confirmed that the pinned D3D12 `copyTexture` path commits transitions and invalidates binding state on each call. Candidate executable `d01c555fb2eb0011d57a6f39bc09d0d03689635d14abf5ef18fefc7de17ff0ea` and matching PDB are archived in workspace `work/runtime/local-shadow-copy-043`, built from parent `9c35334e1f3b09c530de788278e59d6a76d3fd83` plus patch `88774b6baeb945a418e32915f79b887e58eae042eb33acb40203336306ea1e9e`. Configure and Release build exit 0.

BG/BH compare the same executable at 3440x1440, FXAA, AO-high, 16x, grass/conventional shadows and FG off. Serial recording is used in both. Initial profiles, controller, replays, cameras and game clocks match; only BH adds the copy flag. CPU/GPU/slow-frame traces are on, native debug and shadow diagnostics off. All game/PresentMon/sampler exits are 0.

| Scene | Local shadow GPU ms OFF / ON | Renderer CPU ms OFF / ON | Application FPS OFF / ON | Application p99 ms OFF / ON |
|---|---:|---:|---:|---:|
| Interior | 5.013 / 3.317 | 7.685 / 7.952 | 68.53 / 73.31 | 24.99 / 24.09 |
| Outdoor | 4.302 / 3.264 | 7.174 / 8.056 | 62.69 / 62.36 | 26.10 / 29.55 |
| Rain | 4.218 / 2.971 | 7.393 / 9.948 | 63.86 / 56.74 | 27.75 / 34.00 |
| Night | 4.718 / 3.817 | 5.734 / 10.326 | 75.73 / 54.97 | 24.50 / 42.35 |

The recorded GPU pass is 0.90-1.70 ms lower, but whole-frame results do not qualify this change for default enablement. Edge's 3D engine reaches about 19% utilization, with video decoding also active. A separate ECLIPSE geometry audit starts CPU workers during BH (12:03:12-14 UTC, then another at 12:04:35); a subsequent snapshot shows substantial CPU use. That timing is consistent with the later CPU growth, but the snapshot is not a continuous CPU-contention trace. Light/geometry workloads also differ. BG/BH are confounded diagnostic captures, not replacements for the published 0.43 FPS results, and do not isolate either a universal speedup or an intrinsic CPU regression. Thirteen or fourteen GPU samples per scene; application API intervals only, with no displayed-frame/latency/drop evidence.

BI completes two full Zaton unload/reloads with native D3D12/NVRHI validation, DLSS Quality, FSR FG and partitioned recording. All 67 sampled copy-count invariants pass, including cold-cache updates. All 153 inventory contents/condition/ammo and Grouse's 22,927-byte custom data survive. Game and PresentMon exit 0; no matched fatal/native/NVRHI error. Existing native warning IDs 820/821/679 remain.

All eleven screenshots were actually opened and inspected: three reload stills plus four BG/BH pairs. No obvious new geometry, lighting or shadow loss was seen. Interior lighting is consistent; outdoor foliage, rain/fog and the dark night scene remain present. Animation differs, and night darkness limits subtle assessment. Sky/lighting changes across full reload already occur in earlier checkpoints; these are not exact weather or generated-frame temporal-quality tests. Both performance runs retain 836 legacy shader failures.

Decision: retain the opt-in experiment for controlled follow-up; leave normal behavior and delivered 0.43 unchanged. Do not spend more full benchmark runs under the same known competing load. A quieter or appropriately counterbalanced measurement must establish frame-time benefit before promotion. The original/reference installations and main DX9 comparison are preserved; normal separate 0.41 staging executable/PDB/profile are restored and hash-verified. Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_LOCAL_COPY_043_SUMMARY.json`, BG/BH `copy-analysis.json`, and BI `reload-analysis.json`.

Research and source review: 2026-09-09 UTC. Updated with the first native parallel-recording experiment below; it remains disabled by default. Inspected source: `0a754d0127956cef570176e5b88801a415b9b19f`. The baseline at that research checkpoint was 0.41, compiled from `74a3749453254d6b80f68c695fa378373e5ca656`. CPU stage/pass tracing was subsequently implemented and tested on source parent `86742e6580092c0646562b7f67b41627ac7a3d09`. No faster rendering path was accepted at this checkpoint.

The user's priority is to remove internal engine bottlenecks so native x64/DX12 can use modern hardware properly. Put that work ahead of further graphics additions. Retain the current visual quality, complete MISERY behavior, conventional shadows, 16x filtering and RTX HDR-compatible SDR output. Ray tracing remains excluded.

Historical instruction, now superseded: after the failed submission experiment, finish the parallel command-recording architecture before separate shadow optimization. See the current direction above.

## Priority 1: partitioned recording checkpoint (2026-09-09 UTC)

The frame graph can now record multiple owned command lists for one certified pass. Shared preparation runs once, each job owns a context/list and its CPU timing storage, and lists are appended in original pass/partition order. GPU profiling uses distinct partition labels; the original CPU pass label sums CPU work after joining, while group and renderer timings measure wall time. The opt-in `-fg_partition_record` flag requires the existing `-fg_parallel_record` path; normal launches remain unchanged.

The first workload splits local-light recording into two contiguous ranges, balancing visible face counts and keeping every face of a light on one worker. Each range owns its mutable indirect/culling scratch buffers. Cache insertion, eviction and output-array sizing happen before recording; workers own separate cache entries and output slices. Clears and matrix uploads occur on the first list. The existing sun job gives three recording jobs by day; local lights can use two at night. No shader, caster test, cache validity rule, draw order, quality setting or simulation callback changed.

This applies [Microsoft's command-list/allocator ownership guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) and [NVIDIA's advice on balanced parallel command recording](https://developer.nvidia.com/blog/advanced-api-performance-command-buffers/). The pinned NVRHI implementation stores volatile constant-buffer addresses per command list (`Externals/nvrhi/src/d3d12/d3d12-buffer.cpp`); mutable nonvolatile scratch buffers are owned separately. Its existing allocator/fence reuse and dependency-ordered submission remain in use.

Candidate executable `8e4067ea07c83c883ef68c5b07c47b1849234c2d951cca10c43db273bade7f5d` was built from parent `23c15a0bc46ab5f1a3d65c4d9bd9f1ee0fe59a42` plus patch `aed384fd603730382c0090c2aa1050f8bd9b9107aaab8de760e8255544657fd9`. Matching PDB, six source hashes and configure/build logs are archived in workspace `work/runtime/recording-partitions-043`.

BD (`DX12_RECORD_PARTITIONS_OFF_043_BD`) and BE (`DX12_RECORD_PARTITIONS_ON_043_BE`) use that same executable and matching profiles/controller/replays/cameras. Both run at 3440x1440, FXAA, AO-high, 16x, conventional/grass shadows, FG off and parallel recording on; only BE adds partitioning. CPU/GPU/slow-frame tracing is enabled, native debug and full-scan diagnostics disabled. Interior clocks differ by one minute; other clocks match. All game/PresentMon/GPU-sampler exits are 0.

| Scene | Renderer CPU ms OFF / ON | Recording group wall ms OFF / ON | Application FPS OFF / ON | Application p99 ms OFF / ON |
|---|---:|---:|---:|---:|
| Interior | 6.0183 / 5.3091 | 3.246 / 2.522 | 96.59 / 97.09 | 14.74 / 13.49 |
| Outdoor | 5.4300 / 4.7520 | 2.729 / 2.085 | 90.08 / 88.99 | 15.08 / 14.94 |
| Rain | 5.3218 / 4.6764 | 2.595 / 2.030 | 94.03 / 93.63 | 14.60 / 14.06 |
| Night | 5.0985 / 4.5271 | serial fallback / 2.395 | 103.33 / 108.74 | 14.34 / 13.62 |

Renderer CPU is 0.571-0.709 ms lower (about 11-12.5%) in this pair. Total local recording CPU work increases 0.184-0.278 ms; partitioning reduces the critical path but adds overhead. Eighty-five daytime BE samples have three distinct worker threads; 29 night samples have two. The original overlap log directly measures the first two jobs, not a three-way interval. The first local partition remains roughly twice as expensive as the second (interior 2.186 versus 0.976 ms), so face-count balancing is not sufficient.

Native FPS is mixed, with approximately 95-97% GPU utilization and changing light/face workloads. Sampled background engines stay below 0.07%; each scene has fourteen or fifteen GPU samples. This single sequential pair does not establish universal FPS, displayed-frame, latency or long-session gains. PresentMon captures application API intervals only. All eight stills were inspected without obvious new missing geometry, lighting or shadow corruption. Both runs retain 836 legacy shader failures and no matched fatal/device/NVRHI error.

BC (`DX12_RECORD_PARTITIONS_RELOAD_043_BC`) completes two full Zaton unload/reloads with native D3D12/NVRHI validation, DLSS Quality, FSR FG and the partitioned recorder. All 33 recording samples use three distinct threads; 30 face-range samples cover the complete list without overlap or gaps. Fourteen geometry-index checks pass. All 153 inventory contents/condition/ammo and Grouse's 22,927-byte custom data survive; the raw bolt ID changes 11077 to 14848. Exit 0; last successful FG dispatch count 1760. All three stills retain terrain, grass and the weapon. There are 2216 legacy shader failures and no matched fatal/native/NVRHI error; existing native warnings 820/821/679 persist.

BF (`DX12_RECORD_PARTITIONS_RESIZE_043_BF`) completes all five existing FXAA/DLSS/FG cases, including 3440x1440 to 1920x1080 and back, with native validation. Exit 0, 59 three-thread recording samples, last successful FG dispatch count 1582. All five stills were inspected with the NPC, interior geometry and lighting present after recreation. There are 836 legacy shader failures and no matched fatal/native/NVRHI error. These checks do not qualify generated-frame temporal quality, all campaign content or extended stability.

Evidence: workspace `outputs/implementation-evidence/MISERY_DX12_RECORD_PARTITIONS_043_SUMMARY.json`, BC's `reload-analysis.json`, and BF's `resize-analysis.json`. Test controls/probes are archived and restored; normal separate 0.41 executable/PDB/profile are restored and hash-verified. Delivered 0.43 and the existing DX9 comparison remain unchanged. Retain this experimental architecture work. Next, improve recording-job balance using measured cost and address the added recording overhead before another benchmark. Priority 1 remains active; separate shadow/caster algorithm, culling and quality optimization stays deferred.

## Priority 1: rigid upload preparation checkpoint (2026-09-09 UTC)

The collector retains an ordered list of rigid batches whose CPU upload data needs rebuilding: terrain, transparency and dynamic geometry. Once the existing static GPU cache is populated, upload preparation visits that list instead of repeatedly classifying every unchanged static batch. Mutable access, sorting or a material-cache revision invalidates the selection; the initial upload still includes all rigid batches. GPU visibility remains evaluated every frame. This is CPU preparation work, with no change to shaders, shadow algorithms, visibility criteria, quality or game callbacks.

Candidate executable `46b7910c50e80258b34f47c87e2b2e6a3fd768705b889e612229d121a1950b03` was built from parent `644871ab2286995833e36c6f37bbec675cfc6557` plus archived patch `48ecaf7ff031ec5d9c6c5684824a2a5d6650ca71318aefef1bc925187064679b`. Matching PDB, all five tested source hashes and build logs are retained in workspace `work/runtime/rigid-upload-index-043`. The delivered 0.43 package remains unchanged.

AY (the prior candidate capture) and BA (`DX12_RIGID_UPLOAD_CANDIDATE_043_BA`) use matching initial profiles, controller, replays, cameras and clocks at 3440x1440, FXAA, AO-high, 16x, conventional/grass shadows and FG off. Parallel recording and CPU/GPU tracing are on in both; the full-scan index diagnostic is off. Both game/PresentMon/sampler runs exit 0. AY started about 22 minutes before BA; this is one sequential comparison, not a fresh randomized control.

| Scene | Rigid culling CPU ms before / after | Total renderer CPU ms before / after | Application FPS before / after | Application p99 ms before / after |
|---|---:|---:|---:|---:|
| Interior | 0.2830 / 0.1038 | 6.1714 / 6.0181 | 94.90 / 95.10 | 14.72 / 14.12 |
| Outdoor | 0.2893 / 0.1163 | 5.5062 / 5.4168 | 91.34 / 91.92 | 14.77 / 14.42 |
| Rain | 0.2839 / 0.1158 | 5.4246 / 5.2978 | 95.12 / 95.54 | 14.91 / 14.19 |
| Night | 0.2819 / 0.1123 | 5.6725 / 5.5449 | 92.57 / 93.44 | 15.41 / 15.38 |

The rigid pass uses 0.168-0.179 ms less CPU time (about 59-63%) in this pair. Total renderer CPU is 0.089-0.153 ms lower, but changing light/face workloads and approximately 96% GPU utilization prevent attributing the small whole-frame FPS differences to this edit alone. Each BA scene has fourteen GPU samples, with zero sampled non-game engine activity inside the measurement windows. PresentMon measures application intervals; display mode, latency and dropped-frame evidence are unavailable. All four BA stills were inspected without obvious new corruption. Both native captures retain 836 legacy shader failures and no matched fatal/device/NVRHI error.

BB (`DX12_RIGID_UPLOAD_RELOAD_043_BB`) completes two full Zaton unload/reloads with native D3D12 debug/DRED, NVRHI validation, DLSS Quality, FSR FG and parallel recording. The optional full-scan diagnostic verifies both ordered lists in fifteen samples; the rigid update list contains 1,720 entries among roughly 22,000 total batches. All 153 inventory contents/condition/ammo and Grouse's 22,927-byte data survive both reloads; only the raw bolt ID changes from 11077 to 14848. All three reload stills retain terrain, grass and the weapon. Exit 0, 33 distinct-thread overlap samples, last successful FG dispatch count 1733; 2216 legacy shader failures and no matched fatal/native/NVRHI error. Cloud changes across reload, different-level transitions, generated-frame temporal quality and long sessions remain unqualified.

Detailed evidence: workspace `outputs/implementation-evidence/MISERY_DX12_RIGID_UPLOAD_043_SUMMARY.json` and BB's `reload-analysis.json`. The fourteen unique reload probe files are archived, their control restored, and normal 0.41 staging executable/PDB/profile restored and hash-verified. Retain this bounded CPU improvement; parallel stays optional. Priority 1 remains unfinished. Next, inspect partitioning and ownership of the existing recording jobs so more CPU workers can record the same draws safely. Separate shadow/caster algorithm, culling and quality optimization remains priority 2 and deferred.

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
