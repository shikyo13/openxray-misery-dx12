# MISERY 0.46 — native DX12 graphics settings

The existing 0.46 package now starts with **DX12 High - Native** selected. Open **Options → Video → Advanced** for Image, Shadows, World and Effects. Previous/Next reveals the remaining settings in each category. The renderer is explicitly labeled **DirectX 12 (x64)**; this package has one renderer.

Advanced graphics changes and preset selections are staged until **Apply**. **Cancel** discards them. The active preset is identified from the current console values; changes that no longer match a preset display **Custom**. The display calibration sliders show numbers and retain their existing live preview/Cancel behavior. Grass density/range changes require reloading a save or restarting the game.

| Setting | DX12 High - Native |
|---|---|
| Antialiasing | DLAA at native resolution |
| Anisotropic filtering | 16× |
| Textures | Full source assets, fixed in this native loader |
| Sun shadows | On, three 4096-pixel cascades, 180 m far distance |
| Local light shadows | On, 1024-pixel faces, static cache and full face budget |
| Vegetation shadows | On, 100 m distance |
| Ambient occlusion | Ultra; radius 0.8, intensity 1 |
| Sun shafts | High |
| Object detail / view distance | 1.0 / 1.0 |
| Grass density / range | 0.20 spacing / 49 range setting |
| Materials, sky/environment lighting, bloom | Authored material modes; bloom intensity 1 |
| Tree wind / temporal water / soft water / NPC flashlights | On |
| Frame generation / experimental SSGI / ray tracing | Off |

**DX12 High - DLSS Q** uses the same quality settings with DLSS Quality instead of DLAA. AA choices also include Off, FXAA, DLSS Balanced and DLSS Performance. FSR 3 frame generation is a separate choice and can be combined with DLAA/DLSS. Actual activation of these optional combinations still depends on the renderer and hardware; the loaded verification here used DLAA with frame generation off.

Quality presets preserve resolution, window mode, brightness/contrast/gamma, exposure calibration, VSync and frame caps. The current display configuration is 3440×1440 fullscreen, calibration 1/1/1, VSync off, engine cap 501 and menu cap 60. Output is SDR; NVIDIA RTX HDR remains a driver setting. Driver HDR/VRR activation was not measured.

Parallax, depth of field and tessellation are marked **Not implemented**. Their old saved switches do not activate a native DX12 implementation. SSGI is explicitly marked experimental. Diagnostic material modes are not presented as higher quality levels.

The preset definitions are `game/gamedata/configs/dx12_graphics.ltx`. Current saved settings are in `game/_appdata_/user.ltx`. The engine still writes compatibility commands such as `_preset`, `r3_msaa` and `texture_lod`; those are not the native menu's source of truth. The diagnostic launcher already records the full before/after profile and build identity.

**The existing comparison report used a lighter configuration**: FXAA, 2048-pixel sun maps, 512-pixel local shadows, High AO, grass spacing 0.30 and object detail 0.75. Its FPS figures do not measure the new High Native preset. No replacement benchmark was run for this settings change.

Verification on 2026-09-09: native menu Apply/Cancel, preset and Custom state, saved-setting persistence, all settings tabs, and real-time refresh while a loaded game is paused passed. A fresh process loaded the save at 3440×1440 and logged successful native DLAA creation. Menu and game screenshots were inspected. The final loaded test exited 0; pre-existing legacy shader diagnostics also present in the previous 0.46 baseline remain in the log. This is a short functional/visual check, not a long campaign or performance result.
