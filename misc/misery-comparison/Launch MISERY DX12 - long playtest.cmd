@echo off
setlocal
set "MISERY_WORK=C:\Users\Zero\Documents\Codex\2026-09-07\misery-x64-dx12-feasibility\work"
"C:\Python313\python.exe" -B -X utf8 "%MISERY_WORK%\run-long-playtest.py" --package "%~dp0." %*
if errorlevel 1 pause
