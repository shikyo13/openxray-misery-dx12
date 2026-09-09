# Current checkpoint - 2026-09-09

The user authorized reclaiming duplicate storage and replacing the repeated optimization loop with a long user playtest. Do not resume old benchmark, shader or AI-concurrency tasks after compaction.

- Delivered playable build: `D:/Codex/MISERY-DX12/outputs/MISERY_DX12_DEV_0.46`; rollback: 0.45. Both EXE/PDB/profile/manifest hashes were captured before cleanup. Latest maintained engine commit before storage changes: `8b45a2374b6614f454d974545c80f1ad9df5c290`.
- Storage cleanup is in progress: retire 0.1-0.44 after archive verification. Per-package receipts and `work/storage-cleanup-results.json` record completed removals. Never repeat a removal already recorded; never delete the original Steam installation or fixed DX9 reference.
- The optional AI path trace is deferred. Its source patch, EXE/PDB and identity are in `work/runtime/ai-path-047`. CI game/PresentMon/sampler exited 0; the comparison helper failed during CSV archiving with a disk-space error. This is not a completed visual review or optimization result. No 0.47 package exists.
- Protected workspace 0.41 staging EXE/PDB/profile have been restored and hash-verified. No game, sampler or engine build is active. The cleanup process may still be running; inspect its exact session before starting another cleanup.
- Next: complete cleanup and storage guards, commit/push the maintained helpers and this checkpoint, then prepare low-overhead bounded logging for the user's long 0.46 playtest. Do not make further renderer/simulation changes before that playtest unless needed to address a concrete blocker.

The detailed historical evidence remains in PERFORMANCE_PRIORITIES.md and workspace reports. Read specific entries only when needed; do not reread or narrate the whole history on each resume.
