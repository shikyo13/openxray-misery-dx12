param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [switch]$Level,
    [string]$BaselineLog
)
$ErrorActionPreference = 'Stop'
$runtimeRoot = (Resolve-Path -LiteralPath $Runtime).Path
$evidenceRoot = (Resolve-Path -LiteralPath $Evidence).Path
if (Get-Process -Name xr_3da,xrEngine -ErrorAction SilentlyContinue) { throw 'Close the active game before a native menu check.' }
foreach ($drive in @('C','D')) { if ((Get-PSDrive $drive).Free -lt 21GB) { throw "Insufficient free space on $drive" } }
$scriptPath = Join-Path $runtimeRoot 'gamedata/scripts/ui_main_menu.script'
$original = [IO.File]::ReadAllBytes($scriptPath)
$probeName = if ($Level) { 'menu-loaded-settings-probe.lua' } else { 'menu-settings-probe.lua' }
$probe = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $probeName))
$screensDir = Join-Path $runtimeRoot '_appdata_/screenshots'
$beforeScreens = @(Get-ChildItem -LiteralPath $screensDir -File | ForEach-Object { $_.Name })
$logPath = Join-Path $runtimeRoot '_appdata_/logs/openxray_zero.log'
$traceName = if ($Level) { 'dx12_settings_level_probe.tsv' } else { 'dx12_settings_probe.tsv' }
$tracePath = Join-Path $runtimeRoot "_appdata_/$traceName"
if (Test-Path -LiteralPath (Join-Path $evidenceRoot 'menu-result.json')) { throw 'Use a fresh evidence directory for a new attempt.' }
Copy-Item -LiteralPath (Join-Path $runtimeRoot '_appdata_/user.ltx') -Destination (Join-Path $evidenceRoot 'profile-before.ltx')
[IO.File]::WriteAllBytes((Join-Path $evidenceRoot 'ui_main_menu.original.script'), $original)
$priorOptIn = $env:MISERY_SETTINGS_PROBE
$priorLevelOptIn = $env:MISERY_SETTINGS_LEVEL_PROBE
$levelScript = Join-Path $runtimeRoot 'gamedata/scripts/dx12_settings_level_probe.script'
if ($Level -and (Test-Path -LiteralPath $levelScript)) { throw 'Unexpected existing level probe; do not overwrite it.' }
$process = $null
$timedOut = $false
try {
    [IO.File]::WriteAllBytes($scriptPath, ($original + [byte[]](13,10) + $probe))
    $launchArgs = @('-fsltx','fsgame.ltx','-nosplash','-nointro','-local_shadow_batch_copies','-silent_error_mode')
    $timeoutMs = 50000
    if ($Level) {
        $env:MISERY_SETTINGS_LEVEL_PROBE = '1'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dx12_settings_level_probe.script') -Destination $levelScript
        $launchArgs += @('-run_script','dx12_settings_level_probe','-start','server(dx12_normal_sniper/single/alife/load) client(localhost)')
        $timeoutMs = 90000
    } else { $env:MISERY_SETTINGS_PROBE = '1' }
    $process = Start-Process -FilePath (Join-Path $runtimeRoot 'bin/xr_3da.exe') -WorkingDirectory $runtimeRoot -ArgumentList $launchArgs -WindowStyle Normal -PassThru
    if (-not $process.WaitForExit($timeoutMs)) { $timedOut = $true; Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $process.Refresh()
    Copy-Item -LiteralPath $logPath -Destination (Join-Path $evidenceRoot 'menu.log')
    if (Test-Path -LiteralPath $tracePath) { Copy-Item -LiteralPath $tracePath -Destination (Join-Path $evidenceRoot 'menu.tsv') }
    Copy-Item -LiteralPath (Join-Path $runtimeRoot '_appdata_/user.ltx') -Destination (Join-Path $evidenceRoot 'profile-after.ltx')
    $screens = @(Get-ChildItem -LiteralPath $screensDir -File | Where-Object { $_.Name -notin $beforeScreens } | Sort-Object LastWriteTimeUtc)
    foreach ($screen in $screens) { Copy-Item -LiteralPath $screen.FullName -Destination $evidenceRoot }
    $completed = (Test-Path -LiteralPath $tracePath) -and [bool](Select-String -LiteralPath $tracePath -Pattern '^complete$' -Quiet)
    $allErrors = @(Select-String -LiteralPath $logPath -Pattern 'FATAL ERROR|SCRIPT RUNTIME ERROR|Lua Error|\[error\]|DXGI_ERROR|DEVICE_REMOVED' | ForEach-Object { $_.Line })
    $knownLegacy = @()
    if ($BaselineLog) { $knownLegacy = @(Select-String -LiteralPath $BaselineLog -Pattern '^! \[ERROR\] Failed to compile VS for shader: ' | ForEach-Object { $_.Line } | Sort-Object -Unique) }
    $errors = @($allErrors | Where-Object { $_ -notin $knownLegacy } | Sort-Object -Unique)
    $legacyWarnings = @($allErrors | Where-Object { $_ -in $knownLegacy })
    $record = [ordered]@{ kind=$(if ($Level) { 'loaded-level' } else { 'menu' }); pid=$process.Id; exit_code=$process.ExitCode; timed_out=$timedOut; completed=$completed; errors=$errors; baseline_log=$BaselineLog; preexisting_legacy_shader_errors=$legacyWarnings.Count; legacy_error_types=@($legacyWarnings | Sort-Object -Unique); screenshots=@($screens | ForEach-Object { $_.Name }) }
    $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'menu-result.json') -Encoding UTF8
    $record | ConvertTo-Json -Depth 4
    if (-not $completed -or $timedOut -or $process.ExitCode -ne 0 -or $errors.Count) { throw 'Native menu check did not pass; inspect this attempt before continuing.' }
}
finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    [IO.File]::WriteAllBytes($scriptPath, $original)
    $env:MISERY_SETTINGS_PROBE = $priorOptIn
    $env:MISERY_SETTINGS_LEVEL_PROBE = $priorLevelOptIn
    if ($Level -and (Test-Path -LiteralPath $levelScript)) { Remove-Item -LiteralPath $levelScript }
}
