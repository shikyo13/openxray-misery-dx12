param(
    [Parameter(Mandatory=$true)][string[]]$Versions,
    [switch]$Apply
)
$ErrorActionPreference='Stop'
$taskPolicy=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'storage-policy.json') -Raw | ConvertFrom-Json
$taskOutputRoots=@((Join-Path (Split-Path -Parent $PSScriptRoot) 'outputs'), $taskPolicy.package_output_root)
$taskArchiveRoot=[IO.Path]::GetFullPath($taskPolicy.retired_archive_root)
$taskPlan=@()
foreach ($taskVersion in $Versions) {
    if ($taskVersion -notmatch '^\d+\.\d+(?:\.\d+)?$') { throw 'Invalid version.' }
    if ($taskVersion -eq $taskPolicy.current_version) { throw 'The current delivered package is protected.' }
    foreach ($taskOutputRoot in $taskOutputRoots) {
        $taskRoot=(Resolve-Path -LiteralPath $taskOutputRoot).Path
        $taskPackage=Join-Path $taskRoot ('MISERY_DX12_DEV_'+$taskVersion)
        $taskGame=Join-Path $taskPackage 'game'
        if (-not (Test-Path -LiteralPath $taskGame -PathType Container)) { continue }
        $taskResolved=(Resolve-Path -LiteralPath $taskGame).Path
        $taskExpected=[IO.Path]::GetFullPath($taskGame)
        if ($taskResolved -ne $taskExpected -or -not $taskResolved.StartsWith($taskRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Deletion target escaped the package root.' }
        foreach ($taskCheck in @($taskPackage,$taskGame)) {
            if ((Get-Item -LiteralPath $taskCheck).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a linked package.' }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $taskPackage 'manifest.json'))) { throw 'Package manifest missing.' }
        $taskPlan+=@{version=$taskVersion;package=$taskPackage;game=$taskResolved}
    }
}
if (-not $Apply) { $taskPlan | ConvertTo-Json -Depth 4; return }
$taskResults=[Collections.Generic.List[object]]::new()
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'storage-cleanup-results.json')) {
    foreach ($taskEarlier in @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'storage-cleanup-results.json') -Raw | ConvertFrom-Json)) { $taskResults.Add($taskEarlier) }
}
foreach ($taskItem in $taskPlan) {
    if (Get-Process -Name xr_3da,xrEngine -ErrorAction SilentlyContinue) { throw 'A game is running; stop retirement.' }
    Write-Output ('Preserving '+$taskItem.version)
    $taskReceiptText=& python -X utf8 (Join-Path $PSScriptRoot 'archive-retired-package.py') --package $taskItem.package --archive-root $taskArchiveRoot
    if ($LASTEXITCODE -ne 0) { throw ('Preservation failed for '+$taskItem.version) }
    $taskReceipt=$taskReceiptText | ConvertFrom-Json
    $taskVerified=& python -X utf8 (Join-Path $PSScriptRoot 'archive-retired-package.py') --package $taskItem.package --archive-root $taskArchiveRoot --verify-only
    if ($LASTEXITCODE -eq 75) { Write-Output 'Retirement paused before deletion.'; return }
    if ($LASTEXITCODE -ne 0) { throw 'The preserved package changed; no deletion.' }
    # Resolve and constrain the final absolute recursive-delete target again immediately before use.
    $taskFinal=(Resolve-Path -LiteralPath $taskItem.game).Path
    if ($taskFinal -ne $taskItem.game -or $taskFinal -ne ([IO.Path]::GetFullPath((Join-Path $taskItem.package 'game')))) { throw 'Final deletion target mismatch.' }
    if (Get-Process -Name xr_3da,xrEngine -ErrorAction SilentlyContinue) { throw 'A game started; stop retirement.' }
    # Native directory deletion avoids PowerShell's per-file provider overhead.
    # The entire tree was just checked for reparse points and archived above.
    try { [IO.Directory]::Delete($taskFinal,$true) }
    catch [UnauthorizedAccessException] {
        Get-ChildItem -LiteralPath $taskFinal -Recurse -Force -File | Where-Object IsReadOnly | ForEach-Object { $_.IsReadOnly=$false }
        [IO.Directory]::Delete($taskFinal,$true)
    }
    if (Test-Path -LiteralPath $taskFinal) { throw 'Package retirement incomplete.' }
    $taskReceipt.status='Game payload removed after archive CRC, hash and source-inventory verification'
    $taskReceipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskItem.package 'retirement-receipt.json') -Encoding utf8
    $taskNotice="This older game payload was retired to reclaim duplicate assets.`n`nSaves, settings, binaries and sources: $($taskReceipt.archive)`nArchive SHA256: $($taskReceipt.archive_sha256)`nCurrent playable version: $($taskPolicy.current_version)`nOriginal metadata and compatibility overlay remain here.`n"
    Set-Content -LiteralPath (Join-Path $taskItem.package 'RETIRED.md') -Value $taskNotice -Encoding utf8
    Get-ChildItem -LiteralPath $taskItem.package -Filter 'Launch MISERY DX12*.cmd' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination ($_.FullName+'.retired-original.txt')
        Set-Content -LiteralPath $_.FullName -Encoding ascii -Value "@echo off`necho This old package was retired. See RETIRED.md for preserved saves and build files.`npause`n"
    }
    $taskResults.Add($taskReceipt)
    $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'storage-cleanup-results.json') -Encoding utf8
    Write-Output ('Retired '+$taskItem.version+'; removed '+[math]::Round($taskReceipt.game_bytes/1GB,2)+' GiB payload; archive '+[math]::Round($taskReceipt.archive_bytes/1MB,1)+' MiB')
}
