param(
    [Parameter(Mandatory=$true)][string]$Name,
    [ValidateSet('cop','misery')][string]$Runtime='cop',
    [string]$RuntimeRoot,
    [string]$ReferenceGame,
    [string]$GameArguments='-fsltx fsgame.ltx -nosplash -nointro -i -silent_error_mode -ss_tga -run_script dx12_smoke -start server(all/single/alife/new) client(localhost)',
    [int]$MaxSeconds=75,
    [switch]$CaptureCrash,
    [switch]$Visible
)
$ErrorActionPreference='Stop'
$runtimeTaskRoot=if ($RuntimeRoot) { (Resolve-Path -LiteralPath $RuntimeRoot).Path } else { (Resolve-Path (Join-Path $PSScriptRoot "runtime/$Runtime")).Path }
$taskPackageRoots=@((Join-Path (Split-Path -Parent $PSScriptRoot) 'outputs'), 'D:\Codex\MISERY-DX12\outputs')
$taskAllowedRoots=@((Join-Path $PSScriptRoot 'runtime')) + $taskPackageRoots
if (-not ($taskAllowedRoots | Where-Object { $runtimeTaskRoot.StartsWith(([IO.Path]::GetFullPath($_)+'\'),[StringComparison]::OrdinalIgnoreCase) })) {
    throw 'Test runtime must be inside this project runtime or one of its designated package directories.'
}
$runtimeTaskExe=Join-Path $runtimeTaskRoot 'bin/xr_3da.exe'
if (Get-Process -Name xr_3da -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $runtimeTaskExe }) {
    throw 'An isolated test instance is already running.'
}
$taskRecordPath=Join-Path $PSScriptRoot "runtime/$Name-result.json"
$taskLogPath=Join-Path $PSScriptRoot "logs/$Name.log"
if ((Test-Path -LiteralPath $taskRecordPath) -or (Test-Path -LiteralPath $taskLogPath)) {
    throw 'Choose a new evidence name; prior test results are preserved.'
}
$taskBuildExe=Join-Path $PSScriptRoot 'engine/bin/AMD64/Release/xr_3da.exe'
$runtimeTaskPdb=Join-Path $PSScriptRoot 'build/dx12/pdb/Release/xr_3da.pdb'
if ($ReferenceGame) {
    $taskReferenceRoot=(Resolve-Path -LiteralPath $ReferenceGame).Path
    if (-not ($taskPackageRoots | Where-Object { $taskReferenceRoot.StartsWith(([IO.Path]::GetFullPath($_)+'\'),[StringComparison]::OrdinalIgnoreCase) })) {
        throw 'Reference binaries must come from a preserved package in a designated package directory.'
    }
    $taskBuildExe=Join-Path $taskReferenceRoot 'bin/xr_3da.exe'
    $runtimeTaskPdb=Join-Path $taskReferenceRoot 'bin/xr_3da.pdb'
}
Copy-Item -LiteralPath $taskBuildExe -Destination $runtimeTaskExe -Force
if (Test-Path -LiteralPath $runtimeTaskPdb) {
    Copy-Item -LiteralPath $runtimeTaskPdb -Destination (Join-Path $runtimeTaskRoot 'bin/xr_3da.pdb') -Force
}
$env:SDL_WINDOW_ACTIVATE_WHEN_SHOWN=if ($Visible) { '1' } else { '0' }
$env:SDL_WINDOW_ACTIVATE_WHEN_RAISED=if ($Visible) { '1' } else { '0' }
$taskScriptName=$null
$taskScriptHash=$null
if ($GameArguments -match '(?:^|\s)-run_script\s+([A-Za-z0-9_]+)(?:\s|$)') {
    $taskScriptName=$Matches[1]
    $taskScriptHash=(Get-FileHash -LiteralPath (Join-Path $runtimeTaskRoot "gamedata/scripts/$taskScriptName.script") -Algorithm SHA256).Hash
}
$taskWindowStyle=if ($Visible) { 'Normal' } else { 'Hidden' }
$taskProcess=Start-Process -FilePath $runtimeTaskExe -WorkingDirectory $runtimeTaskRoot -ArgumentList $GameArguments -WindowStyle $taskWindowStyle -PassThru
$taskObserver=$null
if ($CaptureCrash) {
    $taskDumpRoot=Join-Path $PSScriptRoot "runtime/dumps/$Name"
    New-Item -ItemType Directory -Path $taskDumpRoot -Force | Out-Null
    $taskDumpTool=Join-Path $PSScriptRoot 'tools/procdump/procdump64.exe'
    $taskObserver=Start-Process -FilePath $taskDumpTool -WindowStyle Hidden -PassThru -ArgumentList @('-accepteula','-e','-mm','-n','1',$taskProcess.Id,('"'+$taskDumpRoot+'"')) -RedirectStandardOutput (Join-Path $PSScriptRoot "logs/$Name-procdump.log") -RedirectStandardError (Join-Path $PSScriptRoot "logs/$Name-procdump-errors.log")
}
$taskRecord=[ordered]@{
    pid=$taskProcess.Id
    runtime=$Runtime
    started=[DateTimeOffset]::UtcNow.ToString('o')
    exe=$runtimeTaskExe
    sha256=(Get-FileHash -LiteralPath $runtimeTaskExe -Algorithm SHA256).Hash
    args=$GameArguments
    max_seconds=$MaxSeconds
    visible=[bool]$Visible
    script_namespace=$taskScriptName
    script_sha256=$taskScriptHash
    profile_sha256=(Get-FileHash -LiteralPath (Join-Path $runtimeTaskRoot '_appdata_/user.ltx') -Algorithm SHA256).Hash
}
$taskRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime/cop-process.json') -Encoding utf8
$taskRecord | ConvertTo-Json
$taskDeadline=[DateTimeOffset]::UtcNow.AddSeconds($MaxSeconds)
$taskExited=$false
do {
    $taskRemaining=[int][math]::Max(1,($taskDeadline-[DateTimeOffset]::UtcNow).TotalMilliseconds)
    $taskExited=$taskProcess.WaitForExit([math]::Min(30000,$taskRemaining))
    if (-not $taskExited) { Write-Output 'Isolated test still running; waiting for scripted exit within the time limit.' }
} while (-not $taskExited -and [DateTimeOffset]::UtcNow -lt $taskDeadline)
$taskRecord.observed=[DateTimeOffset]::UtcNow.ToString('o')
$taskRecord.exited_before_deadline=$taskExited
if (-not $taskExited) {
    $taskRecord.final_disposition='Forced stop at diagnostic time limit; not a clean exit'
    Stop-Process -InputObject $taskProcess -Force
    $taskProcess.WaitForExit()
} else {
    $taskRecord.final_disposition='Process exited before deadline; inspect engine log and exit code'
}
$taskRecord.exit_code=$taskProcess.ExitCode
if ($taskObserver) {
    if (-not $taskObserver.WaitForExit(5000)) {
        & $taskDumpTool -cancel $taskProcess.Id | Out-Null
        $null=$taskObserver.WaitForExit(5000)
    }
    $taskRecord.procdump_pid=$taskObserver.Id
    $taskRecord.dump_directory=$taskDumpRoot
}
$taskRecord | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskRecordPath -Encoding utf8
Copy-Item -LiteralPath (Join-Path $runtimeTaskRoot '_appdata_/logs/openxray_zero.log') -Destination $taskLogPath
$taskRecord | ConvertTo-Json
Select-String -LiteralPath $taskLogPath -Pattern 'DX12Smoke|Screenshot|FATAL|\[NVRHI\] ERROR|Shutdown complete' | Select-Object -Last 30
