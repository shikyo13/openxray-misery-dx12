# MISERY DX12 development build 0.42

Run **Launch MISERY DX12.cmd** for the normal configuration. To try the optional parallel recorder, use **Launch MISERY DX12 - experimental parallel.cmd**. Both launch this package's independent game and profile. Parallel recording remains experimental and is off in the normal launcher.

Load `dx12_normal_sniper` for the included customized Sniper checkpoint. The package also includes `dx12_skadovsk_npc`. Original DX9 saves were not converted or imported. Previous packages and the original installation are retained.

This snapshot delivers the tested resize and full-level reload fixes: world material bindings survive resizing, terrain and grass rebuild correctly after full reload, and DLSS/FSR resource handoffs use the corrected state/lifetime handling. It also includes opt-in owned parallel command recording. No new shadow algorithm, caster reduction or graphics-quality downgrade was added.

The normal profile is restored to 3440×1440, FXAA and 16x filtering, with SDR output for driver RTX HDR. Frame generation is off initially and remains independently selectable in the graphics controls. Ray tracing remains excluded.

The packaged normal renderer completed the four fixed interior/outdoor/rain/night scenes at native resolution and exited cleanly. The same executable previously completed resize and two in-process level reload checks with DLSS Quality and FSR frame generation. These are bounded checks, not full campaign or long-session certification. Existing legacy shader compilation/fallback failures remain; native depth of field and parallax are still unfinished.

Performance evidence is mixed. The native traced pair reduced daytime CPU renderer time by about 11–13%. In a separate 1720×720 pair with trace switches disabled, parallel recording improved daytime application FPS by about 5–12%, while night was about 5% slower despite sequential fallback. Presentation mode, displayed-frame timing, latency and drops were unavailable in those API-only captures. The optional launcher is not a promise of a universal FPS gain.

The refreshed visual/settings comparison is `MISERY_DX9_DX12_COMPARISON_042.html` in the project outputs directory. It retains the fixed DX9 baseline and explicitly records the FXAA and display-capture differences. The older 0.41 comparison is preserved.

Engine source: `e54ae6076994b1302fca6936c4238c7aa5da7de9` in https://github.com/shikyo13/openxray-misery-dx12/tree/codex/misery-dx12 . Executable SHA-256: `d217d8cda0cd688ff83907841c871e450ff957e2ff4ad2b36cb8d56ab6817ddb`. The manifest records copied file hashes and the packaging source snapshot. This is a private development package containing the user's installed game assets.
