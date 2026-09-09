# MISERY 0.46 long playtest

Double-click **Launch MISERY DX12 - long playtest.cmd** in
`D:/Codex/MISERY-DX12/outputs/MISERY_DX12_DEV_0.46`.
Leave its small console running behind the game. It uses the delivered 0.46 executable,
your existing saves and graphics settings, and the normal launcher arguments.

Play normally: combat, trading, inventory, saving/loading, travel and weather changes
are useful coverage. Press **F12** for visual problems and note roughly when a problem
happens and what you were doing. Exit normally when finished, then tell Codex to review
the playtest. Settings you change during play remain changed.

Each run gets a dated folder under `D:/Codex/MISERY-DX12/playtests` containing:

- `session.json`: build/hash, launch arguments, timestamps, exit code and collector status.
- `settings-before.ltx` / `settings-after.ltx`: the actual saved graphics/input profiles.
- `frame-times.jsonl`: five-second summaries per swapchain, including average FPS,
  p95/p99/max present intervals and counts above 16.667/24/50/100 ms.
- `hardware.jsonl`: process memory/CPU and NVIDIA adapter readings every ten seconds.
- `engine-tail.log`: up to the last 16 MiB of the engine log, refreshed once a minute
  and at exit. A bounded copy of the previous log is also preserved before launch.
- `presentmon-errors.jsonl` and `build-identity.json`.

PresentMon measures application **present intervals**, including menus/loading/pauses.
It does not measure displayed/generated frames, input latency or GPU frame duration.
NVIDIA readings include other applications on that adapter. The log contains no heavy
engine frame/AI traces; a specific issue can justify a later short diagnostic capture.

The added files have fixed limits totalling less than 64 MiB per session; the storage
preflight reserves 128 MiB, caps the playtest collection at 2 GiB and keeps 20 GiB free.
Recording lasts at most eight hours. A duration/output/free-space limit stops the
collectors, never the game. Existing evidence is not automatically deleted.
The normal game log, saves, F12 screenshots and native crash reports remain in the
package's `game/_appdata_` folders and are outside the added-diagnostics limit.
Large crash reports and screenshots are referenced there instead of copied per run.
If a collector fails, its status is recorded; an exit code of zero alone is not proof
of a successful campaign or complete diagnostic coverage.

The helper is maintained as `run-long-playtest.py` here and deployed to workspace
`work`, beside `storage-policy.json` and the existing PresentMon executable. The local
launcher uses `C:/Python313/python.exe`; adjust that path if moving the workspace.
Run `python -B work/run-long-playtest.py --check` for a read-only preflight.

Validation for this delivery is recorded in workspace
`outputs/implementation-evidence/LONG_PLAYTEST_SETUP_046/verification.json`.
The launcher was prepared for the user's manual session; no new campaign run or
graphics/performance comparison was performed during setup.
