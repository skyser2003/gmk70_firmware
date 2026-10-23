# Explicit RAM-only physical trial. Build/test commands never invoke this file.
#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Inspect', 'Canary', 'VerifyStock', 'RestoreCanary')]
    [string] $Mode = 'Inspect',
    [Parameter(Mandatory = $true)] [string] $OutputDirectory,
    [string] $StockImagePath = '',
    [switch] $SnapshotFlash
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ram-transport.ps1')
if ($StockImagePath -eq '') { $StockImagePath = Join-Path $PSScriptRoot 'stock-application.bin' }
$stock = [IO.File]::ReadAllBytes($StockImagePath)
$stockSha = '100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c'
if ($stock.Length -ne 0x25000 -or (Get-FileHash -LiteralPath $StockImagePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $stockSha) {
    throw 'The offline stock fixture does not match this exact radio.'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Refusing to overwrite an earlier trial.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$report = [ordered]@{
    mode = $Mode; started_utc = [DateTime]::UtcNow.ToString('o'); status = 'incomplete'
    bluetooth_address = 'c7:02:53:aa:76:69'; callback_activation_attempted = $false
    stock_application_sha256 = $stockSha; source_sha256 = @{}
}
foreach ($name in @('ram-trial.ps1', 'ram-transport.ps1', 'ram-trial-core.ps1', 'radio-core.ps1')) {
    $report.source_sha256[$name] = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$vialExe = 'C:\Program Files (x86)\Vial\Vial.exe'
if (Test-Path -LiteralPath $vialExe) { $report['vial_executable_sha256'] = (Get-FileHash -LiteralPath $vialExe -Algorithm SHA256).Hash.ToLowerInvariant() }
$failed = $true
try {
    Open-Gmk70TrialRadio
    [byte[]] $window = Read-Gmk70TrialRam -Address 0x11001ae4 -Length 226
    [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'private-code-window-before.bin'), $window)
    [byte[]] $expected = $stock[0x19b60..(0x19b60 + 225)]
    $report['ram_code_matches_stock'] = [BitConverter]::ToString($window) -eq [BitConverter]::ToString($expected)
    [byte[]] $canaryWindow = $expected.Clone()
    [Array]::Copy([byte[]] (0, 0x20, 0x70, 0x47), 0, $canaryWindow, 16, 4)
    $report['ram_code_matches_canary'] = [BitConverter]::ToString($window) -eq [BitConverter]::ToString($canaryWindow)
    if ($Mode -eq 'RestoreCanary') {
        if (-not $report.ram_code_matches_canary) { throw 'Only the exact previously staged canary may be restored.' }
    } elseif (-not $report.ram_code_matches_stock) {
        throw 'RAM does not match stock. No further operation or callback activation is permitted.'
    }
    [byte[]] $slots = Read-Gmk70TrialRam -Address 0x110063c0 -Length (8 * 0x118)
    [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'private-gatt-slots.bin'), $slots)
    $callbackSlot = Get-Gmk70OtaCallbackSlot -Bytes $slots
    $report['ota_callback_slot'] = '0x{0:X8}' -f $callbackSlot
    $report['gatt_slots'] = @()
    for ($index = 0; $index -lt 8; $index++) {
        $offset = $index * 0x118
        $report.gatt_slots += @{ index = $index; count = [int] $slots[$offset + 0x108];
            table = '0x{0:X8}' -f [BitConverter]::ToUInt32($slots, $offset + 0x104);
            callback = '0x{0:X8}' -f [BitConverter]::ToUInt32($slots, $offset + 0x10c) }
    }
    [byte[]] $heap = Read-Gmk70TrialRam -Address 0x11000ae4 -Length 64
    [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'private-heap-metadata.bin'), $heap)
    $report['heap_free_list_heads'] = @(0..3 | ForEach-Object { '0x{0:X8}' -f [BitConverter]::ToUInt32($heap, 0x18 + 4 * $_) })
    $captureLength = if ($SnapshotFlash) { 0x80000 } else { 0x25000 }
    Write-Host ('Validating {0} bytes of stock storage. Keep tapping Shift.' -f $captureLength)
    $capture = Join-Path $OutputDirectory 'private-storage-before.bin'
    $captureInfo = Write-Gmk70Region -Path $capture -Length $captureLength -ChunkSize 128 -ReadBlock {
        param($Offset, $Length)
        return ,(Read-Gmk70TrialBlock -Address $Offset -Length $Length -Storage)
    } -OnProgress { param($Done, $Total) Write-Host ('Stock storage: {0}/{1}' -f $Done, $Total) }
    $bytes = [IO.File]::ReadAllBytes($capture)
    $digest = [Security.Cryptography.SHA256]::Create()
    try { $actualSha = [BitConverter]::ToString($digest.ComputeHash($bytes, 0, 0x25000)).Replace('-', '').ToLowerInvariant() }
    finally { $digest.Dispose() }
    if ($actualSha -ne $stockSha) { throw 'Live application differs from the exact stock fixture.' }
    $report['live_stock_application_verified'] = $true
    $report['storage_capture'] = $captureInfo
    # Flash verification and slot inspection can take time. Recheck the code
    # immediately before granting the four-byte RAM scope.
    [byte[]] $current = Read-Gmk70TrialRam -Address 0x11001ae4 -Length 226
    if ([BitConverter]::ToString($current) -ne [BitConverter]::ToString($window)) { throw 'RAM changed since inspection.' }
    if ($Mode -in @('Canary', 'RestoreCanary')) {
        [byte[]] $replacement = if ($Mode -eq 'Canary') { [byte[]] (0, 0x20, 0x70, 0x47) } else { [byte[]] $expected[16..19] }
        $script:trialWriteRanges = @(@{ start = [uint32] 0x11001af4; length = 4 })
        try { Send-Gmk70TrialPacket (New-Gmk70RamWritePacket -Address 0x11001af4 -Bytes $replacement) }
        finally { $script:trialWriteRanges = @() }
        [byte[]] $after = Read-Gmk70TrialRam -Address 0x11001ae4 -Length 226
        [byte[]] $target = if ($Mode -eq 'Canary') { $canaryWindow } else { $expected }
        if ([BitConverter]::ToString($after) -ne [BitConverter]::ToString($target)) { throw 'RAM canary readback mismatch.' }
        [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'private-code-window-after.bin'), $after)
        $report['ram_write_verified'] = $true
        [byte[]] $slotAfter = Read-Gmk70TrialRam -Address $callbackSlot -Length 16
        if ([BitConverter]::ToUInt32($slotAfter, 0) -ne 0x100122ed) { throw 'Stock OTA callback changed unexpectedly.' }
    }
    $report.status = 'verified'
    $failed = $false
    Write-Host ('Verified {0}; callback activation was not attempted.' -f $Mode)
}
catch {
    $report.status = 'failed'
    $report['error'] = $_.Exception.ToString()
    $report['script_stack_trace'] = $_.ScriptStackTrace
    Write-Host ('Trial stopped: {0}' -f $_.Exception.Message)
}
finally {
    Close-Gmk70TrialRadio
    $report['sent_requests'] = @($script:trialSent.ToArray())
    $report['finished_utc'] = [DateTime]::UtcNow.ToString('o')
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'private-report.json'), ($report | ConvertTo-Json -Depth 8), $utf8)
    Write-Host ('Private local evidence: {0}' -f $OutputDirectory)
}
if ($failed) { exit 1 }
