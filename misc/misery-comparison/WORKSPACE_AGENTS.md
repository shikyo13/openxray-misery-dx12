# MISERY project working instructions

- On resume, read `work/progress.md` once for the current checkpoint. The user's latest scope correction overrides historical next-action notes. Do not restart completed investigations after compaction.
- Current priority: finish authorized storage cleanup, then prepare the user's long playtest. Further AI, shader and multicore experiments are deferred until playtest evidence justifies them.
- `work/storage-policy.json` identifies the current and rollback versions and storage limits. Keep at most two full game packages. Routine changes use the existing isolated development runtime; full packaging is for playtests/releases.
- Preserve all user saves and settings before retiring a package. Use the verified retirement helper with explicit versions. Keep the original Steam installation and fixed DX9 reference intact. Never follow a junction/reparse point when cleaning up.
- Maintain source in `work/engine` Git and the private mod overlay in `work/misery-compat` Git. Commit small working changes; preserve unfinished experiments as named patches. Never commit licensed game assets, saves, logs, EXEs or PDBs to the source repository.
- The source-controlled workspace helpers are in `work/engine/misc/misery-comparison`. Keep deployed copies in `work` synchronized. `STATUS.md` supplies `work/progress.md`; `WORKSPACE_AGENTS.md` supplies this root file.
- Check storage before builds and captures. Respect package/evidence limits and free-space reserves. Heavy per-frame tracing belongs in short bounded diagnostics, not unattended long playtests.
- Report actual reclaimed space, completed checks and current limitations. Successful short tests are not full campaign proof. Use existing reports and screenshots rather than rebuilding past comparisons without a concrete need.
- STOP or pause means stop issuing commands immediately, including cleanup and validation.
