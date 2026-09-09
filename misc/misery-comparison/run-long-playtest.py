"""Manual play on the delivered package; bounded external diagnostics, no game deadline."""
import argparse
import csv
import ctypes as ct
from ctypes import wintypes as wt
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone

MIB = 1024 ** 2
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def utc():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    data = json.dumps(value, indent=2).encode("utf-8")
    if len(data) > 256 * 1024:
        raise ValueError("Metadata exceeds its size limit")
    path.write_bytes(data)


def tree_size(root):
    total = 0
    for directory, folders, files in os.walk(root, followlinks=False):
        for name in folders + files:
            path = Path(directory) / name
            if path.is_symlink() or path.is_junction():
                raise ValueError(f"Unexpected link in diagnostics: {path}")
        total += sum((Path(directory) / name).stat().st_size for name in files)
    return total


def copy_tail(source, destination, limit):
    if not source.is_file():
        return None
    with source.open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        stream.seek(max(0, size - limit))
        data = stream.read(limit)
    destination.write_bytes(data)
    return {"source": str(source), "source_bytes": size,
            "copied_bytes": len(data), "truncated_to_tail": size > limit}


class BoundedLines:
    def __init__(self, path, limit):
        self.stream = path.open("xb")
        self.limit, self.size, self.capped = limit, 0, False

    def write(self, value):
        data = (json.dumps(value, separators=(",", ":")) + "\n").encode("utf-8")
        if self.size + len(data) > self.limit:
            self.capped = True
            return
        self.stream.write(data)
        self.stream.flush()
        self.size += len(data)

    def close(self):
        self.stream.close()


class FrameBuckets:
    """Five-second, per-swapchain API interval summaries; never retain raw frames."""
    def __init__(self, output):
        self.output, self.bucket, self.chains = output, None, {}
        self.rows, self.invalid, self.omitted_chains = 0, 0, 0

    def add(self, row):
        try:
            stamp, interval = float(row["TimeInSeconds"]), float(row["msBetweenPresents"])
            if not math.isfinite(stamp) or not math.isfinite(interval) or interval <= 0:
                raise ValueError()
        except (KeyError, ValueError):
            self.invalid += 1
            return
        bucket = math.floor(stamp / 5)
        if self.bucket is not None and bucket != self.bucket:
            self.flush()
        self.bucket = bucket
        chain = row.get("SwapChainAddress", "unknown")
        if chain not in self.chains:
            if len(self.chains) == 16:
                self.omitted_chains += 1
                return
            self.chains[chain] = {"n": 0, "sum": 0., "max": 0., "samples": [],
                                  "over": [0, 0, 0, 0], "qpc_first": row.get("QPCTime")}
        entry = self.chains[chain]
        entry["n"] += 1
        entry["sum"] += interval
        entry["max"] = max(entry["max"], interval)
        entry["qpc_last"] = row.get("QPCTime")
        if len(entry["samples"]) < 10000:
            entry["samples"].append(interval)
        for index, threshold in enumerate((16.667, 24, 50, 100)):
            entry["over"][index] += interval > threshold
        self.rows += 1

    def flush(self):
        for chain, entry in self.chains.items():
            samples = sorted(entry["samples"])
            complete = len(samples) == entry["n"]
            self.output.write({"presentmon_seconds_bucket": self.bucket * 5, "swapchain": chain,
                "frames": entry["n"], "api_fps": 1000 * entry["n"] / entry["sum"],
                "mean_interval_ms": entry["sum"] / entry["n"], "max_interval_ms": entry["max"],
                "p95_interval_ms": samples[math.ceil(len(samples) * .95) - 1] if complete else None,
                "p99_interval_ms": samples[math.ceil(len(samples) * .99) - 1] if complete else None,
                "over_16_667_24_50_100_ms": entry["over"], "percentiles_complete": complete,
                "qpc_first": entry["qpc_first"], "qpc_last": entry["qpc_last"]})
        self.chains.clear()


class MemoryCounters(ct.Structure):
    _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD)] + [
        (name, ct.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
        "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
        "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


class ProcessTelemetry:
    def __init__(self, pid):
        self.kernel = ct.WinDLL("kernel32", use_last_error=True)
        self.psapi = ct.WinDLL("psapi", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
        self.kernel.OpenProcess.restype = wt.HANDLE
        self.kernel.GetProcessTimes.argtypes = [wt.HANDLE] + [ct.POINTER(wt.FILETIME)] * 4
        self.kernel.CloseHandle.argtypes = [wt.HANDLE]
        self.psapi.GetProcessMemoryInfo.argtypes = [wt.HANDLE, ct.POINTER(MemoryCounters), wt.DWORD]
        self.handle = self.kernel.OpenProcess(0x1000 | 0x10, False, pid)
        if not self.handle:
            raise ct.WinError(ct.get_last_error())
        self.previous = None

    def sample(self):
        memory = MemoryCounters()
        memory.cb = ct.sizeof(memory)
        times = [wt.FILETIME() for _ in range(4)]
        if not self.psapi.GetProcessMemoryInfo(self.handle, ct.byref(memory), memory.cb):
            raise ct.WinError(ct.get_last_error())
        if not self.kernel.GetProcessTimes(self.handle, *(ct.byref(value) for value in times)):
            raise ct.WinError(ct.get_last_error())
        cpu = sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in times[2:]) / 1e7
        now = time.monotonic()
        cores = (cpu - self.previous[1]) / (now - self.previous[0]) if self.previous else None
        self.previous = now, cpu
        return {"working_set_bytes": memory.WorkingSetSize, "private_bytes": memory.PrivateUsage,
                "cpu_seconds": cpu, "cpu_core_equivalents": cores,
                "cpu_percent_machine": cores * 100 / (os.cpu_count() or 1) if cores is not None else None}

    def close(self):
        self.kernel.CloseHandle(self.handle)


def gpu_sample():
    command = ["nvidia-smi", "--query-gpu=index,name,driver_version,utilization.gpu,memory.used,"
               "memory.total,temperature.gpu,power.draw", "--format=csv,nounits"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=3, creationflags=HIDDEN)
        if result.returncode:
            return {"error": result.stderr[-1024:]}
        return {"adapter_totals": list(csv.DictReader(result.stdout.splitlines(), skipinitialspace=True))}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"error": str(error)[:1024]}


def preflight(work, package):
    policy = json.loads((work / "storage-policy.json").read_text(encoding="utf-8-sig"))
    expected = Path(policy["package_output_root"]) / ("MISERY_DX12_DEV_" + policy["current_version"])
    package = (package or expected).resolve(strict=True)
    if package != expected.resolve(strict=True):
        raise ValueError("This launcher is for the policy's current delivered package")
    evidence = Path(policy["playtest_evidence_root"])
    used = tree_size(evidence) if evidence.exists() else 0
    # The fixed per-file limits below total less than 64 MiB; leave headroom for metadata.
    if policy["max_playtest_session_mib"] < 64:
        raise ValueError("The session reserve must cover the fixed 64 MiB diagnostic envelope")
    reserve = int(policy["max_playtest_session_mib"] * MIB)
    if used + reserve > policy["max_playtest_evidence_gib"] * 1024 ** 3:
        raise ValueError("Playtest evidence budget is full; existing sessions have been preserved")
    drives = {evidence.anchor, package.anchor, Path(os.environ["SystemRoot"]).anchor}
    minimum = int(policy["minimum_system_free_gib"] * 1024 ** 3)
    for drive in drives:
        if shutil.disk_usage(drive).free < minimum + reserve:
            raise ValueError(f"Insufficient free-space reserve on {drive}")
    identity = json.loads((package / "build-identity.json").read_text(encoding="utf-8-sig"))
    exe = package / "game/bin/xr_3da.exe"
    with exe.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest.lower() != identity["exe_sha256"].lower():
        raise ValueError("Delivered executable does not match its build identity")
    pm = work / "tools/presentmon/PresentMon-2.5.1-x64.exe"
    if not pm.is_file():
        raise FileNotFoundError(pm)
    listing = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True,
                             text=True, check=True, creationflags=HIDDEN)
    if any(row and row[0].lower() in ("xr_3da.exe", "xrengine.exe")
           for row in csv.reader(listing.stdout.splitlines())):
        raise ValueError("Close the running STALKER instance before starting another playtest")
    return policy, package, evidence, identity, pm, drives, minimum, used


def run(work, package, check=False):
    policy, package, evidence, identity, pm_exe, drives, minimum, used = preflight(work, package)
    if check:
        print(json.dumps({"ready": True, "game_launched": False, "package": str(package),
            "exe_sha256": identity["exe_sha256"], "evidence_bytes": used,
            "diagnostic_hours": policy["max_playtest_diagnostic_hours"],
            "session_reserve_mib": policy["max_playtest_session_mib"]}, indent=2))
        return
    started = time.time()
    folder = evidence / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"_{os.getpid()}")
    folder.mkdir(parents=True, exist_ok=False)
    game_root = package / "game"
    appdata = game_root / "_appdata_"
    engine_log = appdata / "logs" / ("openxray_" + os.environ["USERNAME"].lower() + ".log")
    profile = appdata / "user.ltx"
    record = {"started_utc": utc(), "status": "starting", "package": str(package),
        "exe_sha256": identity["exe_sha256"], "source_commit": identity["committed_source"],
        "arguments": ["-fsltx", "fsgame.ltx", "-nosplash", "-local_shadow_batch_copies"],
        "diagnostic_hours_limit": policy["max_playtest_diagnostic_hours"],
        "native_logs": str(appdata / "logs"), "native_crash_reports": str(appdata / "reports"),
        "saves": str(appdata / "savedgames"), "screenshots": str(appdata / "screenshots"),
        "metrics": "Five-second API present intervals per swapchain; includes menus/loading/pauses. "
                   "Not display FPS, generated-frame counts, GPU frame time or input latency. "
                   "NVIDIA samples are adapter totals, including other applications.",
        "storage_scope": "Added diagnostics only; game-owned saves, screenshots, reports and normal log "
                         "remain in the package. Oversized engine logs are copied as a 16 MiB tail."}
    copy_tail(profile, folder / "settings-before.ltx", 256 * 1024)
    record["previous_log"] = copy_tail(engine_log, folder / "previous-engine-tail.log", 4 * MIB)
    write_json(folder / "build-identity.json", identity)
    write_json(folder / "session.json", record)
    frame_output = BoundedLines(folder / "frame-times.jsonl", 32 * MIB)
    hardware_output = BoundedLines(folder / "hardware.jsonl", 8 * MIB)
    pm_errors = BoundedLines(folder / "presentmon-errors.jsonl", 64 * 1024)
    buckets = FrameBuckets(frame_output)
    game = pm = telemetry = None
    threads = []
    session_name = "MISERY-Playtest-" + folder.name
    duration = int(policy["max_playtest_diagnostic_hours"] * 3600)
    collector_error = []

    def read_frames():
        try:
            for row in csv.DictReader(pm.stdout):
                buckets.add(row)
        except Exception as error:
            collector_error.append(str(error)[:1024])
        finally:
            buckets.flush()

    def read_errors():
        for line in pm.stderr:
            pm_errors.write({"utc": utc(), "message": line[:4096].rstrip()})

    def stop_presentmon():
        if pm is None or pm.poll() is not None:
            return
        try:
            subprocess.run([str(pm_exe), "--session_name", session_name, "--terminate_existing_session"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5,
                           creationflags=HIDDEN)
            pm.wait(timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            if pm.poll() is None:
                pm.terminate()  # Only this collector; never terminate the user's game.
                try:
                    pm.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    record["presentmon_stop_warning"] = "Collector did not exit after termination"

    try:
        print(f"Playtest diagnostics: {folder}", flush=True)
        game = subprocess.Popen([str(game_root / "bin/xr_3da.exe"), *record["arguments"]], cwd=game_root)
        record.update(game_pid=game.pid, status="playing")
        write_json(folder / "session.json", record)
        pm = subprocess.Popen([str(pm_exe), "--process_id", str(game.pid), "--output_stdout",
            "--no_console_stats", "--no_track_input", "--no_track_display", "--no_track_gpu",
            "--qpc_time", "--v1_metrics", "--session_name", session_name,
            "--terminate_on_proc_exit", "--timed", str(duration), "--terminate_after_timed"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", creationflags=HIDDEN)
        record["presentmon_pid"] = pm.pid
        frequency = ct.c_longlong()
        ct.windll.kernel32.QueryPerformanceFrequency(ct.byref(frequency))
        record["qpc_frequency"] = frequency.value
        for target in (read_frames, read_errors):
            thread = threading.Thread(target=target, daemon=True)
            thread.start()
            threads.append(thread)
        telemetry = ProcessTelemetry(game.pid)
        deadline, next_sample, next_archive = time.monotonic() + duration, 0., 0.
        collecting = True
        while game.poll() is None:
            now = time.monotonic()
            if collecting and now >= next_sample:
                qpc = ct.c_longlong()
                ct.windll.kernel32.QueryPerformanceCounter(ct.byref(qpc))
                try:
                    hardware_output.write({"utc": utc(), "qpc": qpc.value,
                                           "process": telemetry.sample(), "nvidia": gpu_sample()})
                except OSError as error:
                    record["telemetry_error"] = str(error)
                free = {drive: shutil.disk_usage(drive).free for drive in drives}
                reason = ("frame collector error" if collector_error else
                          "duration limit" if now >= deadline else
                          "free-space reserve" if min(free.values()) < minimum else
                          "output limit" if frame_output.capped or hardware_output.capped else None)
                record.update(last_sample_utc=utc(), frame_rows=buckets.rows, free_bytes=free)
                if pm.poll() is not None:
                    record["presentmon_exit_code"] = pm.returncode
                    if not buckets.rows:
                        record["frame_capture_warning"] = "PresentMon exited without frame data; see its error log"
                if reason:
                    collecting = False
                    record["diagnostics_stopped_reason"] = reason
                    stop_presentmon()
                    print(f"Diagnostics stopped: {reason}. Your game remains running.", flush=True)
                if collecting and now >= next_archive:
                    if engine_log.exists() and engine_log.stat().st_mtime >= started:
                        record["engine_log_copy"] = copy_tail(engine_log, folder / "engine-tail.log", 16 * MIB)
                    next_archive = now + 60
                write_json(folder / "session.json", record)
                next_sample = now + 10
            try:
                game.wait(timeout=1 if collecting else 30)
            except subprocess.TimeoutExpired:
                pass
        record.update(status="game_exited", game_exit_code=game.returncode)
    except (Exception, KeyboardInterrupt) as error:
        record.update(status="monitor_stopped", monitor_error=str(error))
        print(f"Diagnostics stopped: {error}. Any running game is left open.", flush=True)
    finally:
        stop_presentmon()
        for thread in threads:
            thread.join(timeout=5)
        if telemetry:
            telemetry.close()
        for output in (frame_output, hardware_output, pm_errors):
            output.close()
        record.update(ended_utc=utc(), game_exit_code=game.poll() if game else None,
                      presentmon_exit_code=pm.poll() if pm else None, frame_rows=buckets.rows,
                      invalid_frame_rows=buckets.invalid, omitted_swapchains=buckets.omitted_chains,
                      collector_errors=collector_error, frame_output_capped=frame_output.capped,
                      hardware_output_capped=hardware_output.capped)
        # Bounded final copies; large native crash dumps/screenshots stay at the recorded paths.
        if min(shutil.disk_usage(drive).free for drive in drives) >= minimum:
            copy_tail(profile, folder / "settings-after.ltx", 256 * 1024)
            if engine_log.exists() and engine_log.stat().st_mtime >= started:
                record["engine_log_copy"] = copy_tail(engine_log, folder / "engine-tail.log", 16 * MIB)
        write_json(folder / "session.json", record)
        print(f"Session saved: {folder}", flush=True)
    if record["status"] == "monitor_stopped":
        raise RuntimeError("Playtest diagnostics stopped early; inspect session.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--check", action="store_true", help="Read-only preflight; no game or collector launch")
    arguments = parser.parse_args()
    try:
        run(Path(__file__).resolve().parent, arguments.package, arguments.check)
    except Exception as error:
        print(f"Playtest launcher: {error}", file=sys.stderr)
        sys.exit(1)
