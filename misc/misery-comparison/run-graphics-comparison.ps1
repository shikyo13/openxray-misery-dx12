param(
    [Parameter(Mandatory=$true)][string]$Name,
    [Parameter(Mandatory=$true)][ValidateSet('dx9','dx12')][string]$Renderer,
    [int]$MaxSeconds=330,
    [switch]$RetailBenchmark,
    [switch]$Visible,
    [switch]$Lossless,
    [switch]$PresentApiOnly,
    [string]$ExtraArguments='',
    [string]$RuntimeRoot,
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$ScriptNamespace='graphics_comparison_baseline'
)
$ErrorActionPreference='Stop'
if ($Renderer -eq 'dx9' -and $ScriptNamespace -ne 'graphics_comparison_baseline') {
    throw 'The retail callback is fixed to the verified baseline controller.'
}
$comparisonRoot = if ($Renderer -eq 'dx9') { 'D:\Codex\MISERY-DX12\reference\DX9_MAIN_2026-09-08' } else { Join-Path $PSScriptRoot 'runtime/misery' }
if ($RuntimeRoot) {
    if ($Renderer -ne 'dx12') { throw 'RuntimeRoot override is for native DX12 packages only.' }
    $comparisonRoot=$RuntimeRoot
}
$comparisonRoot = (Resolve-Path -LiteralPath $comparisonRoot).Path
$comparisonExe = Join-Path $comparisonRoot $(if ($Renderer -eq 'dx9') { 'bin/xrEngine.exe' } else { 'bin/xr_3da.exe' })
if (Get-Process -Name xrEngine,xr_3da -ErrorAction SilentlyContinue) { throw 'Another game instance is running; do not overlap performance captures.' }
$comparisonEvidence = Join-Path (Split-Path -Parent $PSScriptRoot) ('outputs/implementation-evidence/'+$Name)
if (Test-Path -LiteralPath $comparisonEvidence) { throw 'Preserve existing comparison evidence; use a new name.' }
New-Item -ItemType Directory -Path $comparisonEvidence | Out-Null
$comparisonEvents = Join-Path $comparisonRoot $(if ($Renderer -eq 'dx9') {'_appdata_/logs/xray_zero.log'} else {'_appdata_/logs/openxray_zero.log'})
if (Test-Path -LiteralPath $comparisonEvents) {
    Copy-Item -LiteralPath $comparisonEvents -Destination (Join-Path $comparisonEvidence 'previous-events.tsv')
}
Copy-Item -LiteralPath (Join-Path $comparisonRoot '_appdata_/user.ltx') -Destination (Join-Path $comparisonEvidence 'profile-initial.ltx')
Copy-Item -LiteralPath (Join-Path $comparisonRoot ('gamedata/scripts/'+$ScriptNamespace+'.script')) -Destination (Join-Path $comparisonEvidence 'controller.script')
$comparisonArgs = if ($Renderer -eq 'dx9') {
    '-fsltx D:\Codex\MISERY-DX12\reference\DX9_MAIN_2026-09-08\comparison.ltx -nosplash -nointro -i -silent_error_mode -start server(dx9_reference_skadovsk/single/alife/load) client(localhost)'
} else {
    '-fsltx fsgame.ltx -nosplash -nointro -i -silent_error_mode -run_script '+$ScriptNamespace+' -start server(dx12_normal_sniper/single/alife/load) client(localhost)'
}
if ($RetailBenchmark) {
    if ($Renderer -ne 'dx9') { throw 'Retail benchmark mode is only supported by this original DX9 executable.' }
    $comparisonBenchmarkText="[benchmark]`nbaseline_once = -r2 -ltx user.ltx -nosplash -nointro -i -silent_error_mode -start server(dx9_reference_skadovsk/single/alife/load) client(localhost)`n"
    $comparisonBenchmarkText | Set-Content -LiteralPath (Join-Path $comparisonRoot '_appdata_/baseline_once.ltx') -Encoding ascii
    Copy-Item -LiteralPath (Join-Path $comparisonRoot '_appdata_/baseline_once.ltx') -Destination (Join-Path $comparisonEvidence 'benchmark.ltx')
    $comparisonArgs='-fsltx D:\Codex\MISERY-DX12\reference\DX9_MAIN_2026-09-08\comparison.ltx -nosplash -nointro -batch_benchmark baseline_once.ltx'
}
if ($Lossless) { $comparisonArgs += ' -ss_tga' }
if ($ExtraArguments) { $comparisonArgs += ' ' + $ExtraArguments }
$env:SDL_WINDOW_ACTIVATE_WHEN_SHOWN=if ($Visible) {'1'} else {'0'}
$env:SDL_WINDOW_ACTIVATE_WHEN_RAISED=if ($Visible) {'1'} else {'0'}
if ($Renderer -eq 'dx9') {
    # Supply the existing licensed Steam application's normal launch identity.
    $env:SteamAppId='41700'
    $env:SteamGameId='41700'
}
$comparisonStart=[DateTimeOffset]::UtcNow
$comparisonWindowStyle=if ($Visible) {'Normal'} else {'Hidden'}
$comparisonProcess=Start-Process -FilePath $comparisonExe -WorkingDirectory $comparisonRoot -ArgumentList $comparisonArgs -WindowStyle $comparisonWindowStyle -PassThru
$pmExe=Join-Path $PSScriptRoot 'tools/presentmon/PresentMon-2.5.1-x64.exe'
$pmArgs=@('--process_id',$comparisonProcess.Id,'--output_file',('"'+(Join-Path $comparisonEvidence 'presents.csv')+'"'),
    '--no_console_stats','--no_track_input','--qpc_time','--v1_metrics','--session_name',('Codex-'+$Name),
    '--terminate_on_proc_exit','--timed',($MaxSeconds+5),'--terminate_after_timed')
if ($PresentApiOnly) { $pmArgs += @('--no_track_display','--no_track_gpu') }
$pmProcess=Start-Process -FilePath $pmExe -ArgumentList $pmArgs -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $comparisonEvidence 'presentmon.log') -RedirectStandardError (Join-Path $comparisonEvidence 'presentmon-errors.log')
$comparisonRecord=[ordered]@{renderer=$Renderer; exe=$comparisonExe; pid=$comparisonProcess.Id; args=$comparisonArgs; script_namespace=$ScriptNamespace;
    sha256=(Get-FileHash -LiteralPath $comparisonExe -Algorithm SHA256).Hash; started=$comparisonStart.ToString('o');
    presentmon_pid=$pmProcess.Id; presentmon_display_tracking=(-not [bool]$PresentApiOnly); presentmon_gpu_tracking=(-not [bool]$PresentApiOnly);
    qpc_frequency=[Diagnostics.Stopwatch]::Frequency; max_seconds=$MaxSeconds; visible=[bool]$Visible}
$comparisonRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $comparisonEvidence 'process.json') -Encoding utf8
$comparisonRecord | ConvertTo-Json
$comparisonObserved=[System.Collections.Generic.List[object]]::new()
$comparisonSeen=0
$comparisonReadOffset=0L
$comparisonPartial=''
$comparisonRows=[System.Collections.Generic.List[string]]::new()
$comparisonUniqueRows=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$comparisonDeadline=$comparisonStart.AddSeconds($MaxSeconds)
$comparisonReportAt=$comparisonStart
while (-not $comparisonProcess.HasExited -and [DateTimeOffset]::UtcNow -lt $comparisonDeadline) {
    if ((Test-Path -LiteralPath $comparisonEvents) -and (Get-Item -LiteralPath $comparisonEvents).LastWriteTimeUtc -ge $comparisonStart.UtcDateTime) {
        $comparisonStream=[IO.File]::Open($comparisonEvents,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        if ($comparisonStream.Length -lt $comparisonReadOffset) { $comparisonReadOffset=0L; $comparisonPartial='' }
        $null=$comparisonStream.Seek($comparisonReadOffset,[IO.SeekOrigin]::Begin)
        $comparisonReader=[IO.StreamReader]::new($comparisonStream)
        $comparisonText=$comparisonPartial+$comparisonReader.ReadToEnd()
        $comparisonReadOffset=$comparisonStream.Position
        $comparisonReader.Dispose()
        $comparisonLines=$comparisonText -split "`n"
        $comparisonPartial=$comparisonLines[-1]
        for ($comparisonLine=0; $comparisonLine -lt ($comparisonLines.Count-1); $comparisonLine++) {
            if ($comparisonLines[$comparisonLine] -match 'comparison_event\|(.+)$') {
                $comparisonRow=$Matches[1].Trim().Replace('|',"`t")
                if (-not $comparisonUniqueRows.Add($comparisonRow)) { continue }
                $comparisonRows.Add($comparisonRow)
                $comparisonObserved.Add([ordered]@{qpc=[Diagnostics.Stopwatch]::GetTimestamp();utc=[DateTimeOffset]::UtcNow.ToString('o');line=$comparisonRow})
                Write-Output $comparisonRow
            }
        }
    }
    if (([DateTimeOffset]::UtcNow-$comparisonReportAt).TotalSeconds -ge 30) {
        Write-Output 'Waiting for isolated scripted comparison to finish.'
        $comparisonReportAt=[DateTimeOffset]::UtcNow
    }
    $null=$comparisonProcess.WaitForExit(50)
}
$comparisonRecord.exited_before_deadline=$comparisonProcess.HasExited
if (-not $comparisonProcess.HasExited) {
    Stop-Process -InputObject $comparisonProcess -Force
    $comparisonProcess.WaitForExit()
}
$comparisonRecord.exit_code=$comparisonProcess.ExitCode
$comparisonRecord.ended=[DateTimeOffset]::UtcNow.ToString('o')
if (-not $pmProcess.WaitForExit(10000)) {
    & $pmExe --session_name ('Codex-'+$Name) --terminate_existing_session | Out-Null
    $null=$pmProcess.WaitForExit(5000)
}
$comparisonRecord.presentmon_exit_code=if ($pmProcess.HasExited) { $pmProcess.ExitCode } else { $null }
$comparisonObserved | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $comparisonEvidence 'observed-events.json') -Encoding utf8
$comparisonRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $comparisonEvidence 'result.json') -Encoding utf8
$comparisonLog=Join-Path $comparisonRoot $(if ($Renderer -eq 'dx9') {'_appdata_/logs/xray_zero.log'} else {'_appdata_/logs/openxray_zero.log'})
if (Test-Path -LiteralPath $comparisonLog) { Copy-Item -LiteralPath $comparisonLog -Destination (Join-Path $comparisonEvidence 'engine.log') }
$comparisonGpuLog=Join-Path $comparisonRoot '_appdata_/logs/dx12_graphics.csv'
if ($Renderer -eq 'dx12' -and (Test-Path -LiteralPath $comparisonGpuLog) -and (Get-Item -LiteralPath $comparisonGpuLog).LastWriteTimeUtc -ge $comparisonStart.UtcDateTime) {
    Copy-Item -LiteralPath $comparisonGpuLog -Destination (Join-Path $comparisonEvidence 'dx12_graphics.csv')
}
$comparisonRows | Set-Content -LiteralPath (Join-Path $comparisonEvidence 'controller.tsv') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $comparisonRoot '_appdata_/user.ltx') -Destination (Join-Path $comparisonEvidence 'profile-after.ltx')
foreach ($comparisonShotFolder in @('_appdata_/screenshots','_appdata_/logs','_appdata_')) {
    $comparisonShots=Join-Path $comparisonRoot $comparisonShotFolder
    if (Test-Path -LiteralPath $comparisonShots) {
    Get-ChildItem -LiteralPath $comparisonShots -File | Where-Object {$_.LastWriteTimeUtc -ge $comparisonStart.UtcDateTime -and $_.Extension -in @('.jpg','.png','.tga','.bmp')} | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $comparisonEvidence $_.Name)
    }
    }
}
$comparisonRecord | ConvertTo-Json
