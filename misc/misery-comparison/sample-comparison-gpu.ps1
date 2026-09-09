param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$taskRun=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$taskProcessRecord=Get-Content -LiteralPath (Join-Path $taskRun 'process.json') -Raw | ConvertFrom-Json
$taskGame=Get-Process -Id $taskProcessRecord.pid -ErrorAction Stop
if ($taskGame.Path -ne $taskProcessRecord.exe) { throw 'Process identity mismatch.' }
$taskOutput=Join-Path $taskRun 'gpu-engine-samples.jsonl'
if (Test-Path -LiteralPath $taskOutput) { throw 'Preserve existing telemetry.' }
$taskWriter=[IO.StreamWriter]::new($taskOutput,$false,[Text.UTF8Encoding]::new($false))
$taskWriter.AutoFlush=$true
$taskStart=[DateTimeOffset]::UtcNow
$taskSamples=0
$taskCounterErrors=@()
try {
    Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 2 -MaxSamples 180 -ErrorAction SilentlyContinue -ErrorVariable +taskCounterErrors | ForEach-Object {
        if ($taskGame.HasExited) { throw 'Measured process has exited.' }
        $taskQpc=[Diagnostics.Stopwatch]::GetTimestamp()
        $taskInvalidCounters=@($_.CounterSamples | Where-Object Status -ne 0).Count
        $taskEngines=@($_.CounterSamples | Where-Object { $_.Status -eq 0 -and $_.CookedValue -gt 0.05 } | ForEach-Object {
            $taskEngine=$_
            $taskOwnerId=$null
            $taskOwnerName=$null
            if ($taskEngine.InstanceName -match '^pid_(\d+)_') {
                $taskOwnerId=[int]$Matches[1]
                $taskOwnerName=(Get-Process -Id $taskOwnerId -ErrorAction SilentlyContinue).ProcessName
            }
            [ordered]@{instance=$taskEngine.InstanceName;pid=$taskOwnerId;process=$taskOwnerName;utilization_percent=$taskEngine.CookedValue}
        })
        $taskNv=@(& nvidia-smi --query-gpu=utilization.gpu,memory.used,power.draw --format=csv,noheader,nounits)
        $taskWriter.WriteLine(([ordered]@{qpc=$taskQpc;utc=[DateTimeOffset]::UtcNow.ToString('o');invalid_counter_count=$taskInvalidCounters;engines=$taskEngines;nvidia_gpu_memory_power=$taskNv} | ConvertTo-Json -Compress -Depth 6))
        $taskSamples++
    }
} catch {
    if (-not $taskGame.HasExited) { throw }
} finally {
    $taskWriter.Dispose()
    [ordered]@{game_pid=$taskProcessRecord.pid;exe=$taskProcessRecord.exe;started=$taskStart.ToString('o');ended=[DateTimeOffset]::UtcNow.ToString('o');samples=$taskSamples;sample_interval_seconds=2;engine_inclusion_threshold_percent=0.05;qpc_frequency=[Diagnostics.Stopwatch]::Frequency;counter_errors=@($taskCounterErrors | ForEach-Object { $_.ToString() } | Select-Object -Unique);note='Per-engine counters; do not sum engines as overall GPU utilization. NVIDIA raw columns are GPU utilization percent, memory MiB, power watts. Invalid counters are excluded and counted.'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRun 'gpu-sampling.json') -Encoding utf8
}
