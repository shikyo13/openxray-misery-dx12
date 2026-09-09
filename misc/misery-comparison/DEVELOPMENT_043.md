# MISERY DX12 development build 0.43

Run **Launch MISERY DX12.cmd** for the normal configuration. **Launch MISERY DX12 - experimental parallel.cmd** enables the optional parallel recorder. Both launch this package's independent game and profile. Normal recording remains serial.

Load `dx12_normal_sniper` for the customized Sniper checkpoint, or `dx12_skadovsk_npc`. Original DX9 saves have not been converted or imported. The original installation and previous packages are preserved.

This build retains the level's static geometry batches between frames instead of repeatedly destroying and copying their buffer references. Prior traced tests measured about 0.48 ms less CPU collection work per frame. The same static-retention path passed two full level reloads with DLSS Quality, FSR frame generation and native validation before packaging. The source also adds a session-only `fg_parallel_record 0/1` console switch; it does not change the next launch's profile. No shader, shadow algorithm, graphics quality or simulation callback was changed in this milestone.

The restored normal profile uses 3440×1440, FXAA and 16x filtering, with SDR output for driver RTX HDR. Frame generation is off initially and remains independently selectable. Ray tracing remains excluded.

The actual 0.43 package completed interior, outdoor, rain and night checks in both recording modes, with all eight stills inspected and both game exits 0. Normal-mode application FPS was 101.8 / 93.5 / 97.6 / 113.1; parallel was 101.4 / 91.4 / 98.6 / 118.5. GPU use averaged about 94–97%. Changing game workloads and these mixed results do not establish a consistent native FPS or frame-time gain from parallel recording; it remains optional.

These samples used CPU/GPU pass timers off, one workload log per second, and API-only PresentMon. Actual display mode, displayed-frame timing, latency, drops, VRR and driver RTX HDR output were not measured. The sliders compare the fixed DX9 baseline with the normal 0.43 path at their declared settings; the two renderers still differ in features and costs.

See `MISERY_DX9_DX12_COMPARISON_043.html` in the project outputs directory for sliders, settings and measurements. Older reports remain available. The package still logs existing legacy shader compilation/fallback failures; native depth of field and parallax, complete effects parity, campaign progression and extended-session qualification remain unfinished. This is a development snapshot.

Engine source: `a70a2ee050f16f04432e86a7b6db012046e4d86f` in https://github.com/shikyo13/openxray-misery-dx12/tree/codex/misery-dx12 . Executable SHA-256: `39a6c9683fef395ec749d5f539312d76b0c42d1eae50ad8fa3a1cf1a2d27e2e9`. The matching PDB, initial file hashes and actual package checks are recorded in the manifest. This private package contains the user's installed game assets.
