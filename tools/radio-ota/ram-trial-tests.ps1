# Offline protocol/guard tests. No Bluetooth API is loaded or used.
#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ram-trial-core.ps1')

function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) { throw "$Label expected '$Expected', received '$Actual'." }
}
function Assert-Rejected([scriptblock] $Action, [string] $Label) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "$Label was accepted." }
}

$read = New-Gmk70RamReadPacket -Address 0x11001af4 -Length 16
Assert-Equal ([BitConverter]::ToString($read)) '08-06-00-F4-1A-00-11-10-00' 'RAM read wire format'
Assert-Gmk70TrialPacket -Packet $read
foreach ($address in @([uint32] 0x10000000, [uint32] 0x1100dff8, [uint32] 0x40000000)) {
    Assert-Rejected { New-Gmk70RamReadPacket -Address $address -Length 16 } 'Out-of-RAM read'
}

$scope = @(@{ start = [uint32] 0x11001af4; length = 4 })
$write = New-Gmk70RamWritePacket -Address 0x11001af4 -Bytes ([byte[]] (0, 0x20, 0x70, 0x47))
Assert-Equal ([BitConverter]::ToString($write)) '07-0A-00-F4-1A-00-11-04-00-00-20-70-47' 'RAM write wire format'
Assert-Gmk70TrialPacket -Packet $write -WriteRanges $scope
Assert-Rejected { Assert-Gmk70TrialPacket -Packet $write } 'Writes without a live scope'
$wrongScope = @(@{ start = [uint32] 0x11001af8; length = 4 })
Assert-Rejected { Assert-Gmk70TrialPacket -Packet $write -WriteRanges $wrongScope } 'Write outside exact scope'
$flashPacket = [byte[]] (7, 10, 0, 0, 0, 0, 0x10, 4, 0, 0, 0x20, 0x70, 0x47)
$flashScope = @(@{ start = [uint32] 0x10000000; length = 4 })
Assert-Rejected { Assert-Gmk70TrialPacket -Packet $flashPacket -WriteRanges $flashScope } 'Forged flash packet and flash scope'
foreach ($address in @([uint32] 0, [uint32] 0x10000000, [uint32] 0x1100dfff, [uint32] 0x40000000)) {
    Assert-Rejected { New-Gmk70RamWritePacket -Address $address -Bytes ([byte[]] (0, 0x20, 0x70, 0x47)) } 'Out-of-RAM write'
}
foreach ($opcode in @(0, 1, 2, 3, 4, 5, 9, 255)) {
    $forbidden = [byte[]] (6, 6, 0, 0, 0, 0, 0, 16, 0)
    $forbidden[0] = $opcode
    Assert-Rejected { Assert-Gmk70TrialPacket -Packet $forbidden -WriteRanges $scope } 'Destructive/unknown command'
}
$corrupt = [byte[]] $write.Clone()
$corrupt[1] = 9
Assert-Rejected { Assert-Gmk70TrialPacket -Packet $corrupt -WriteRanges $scope } 'Mismatched wire length'
Assert-Rejected { New-Gmk70RamWritePacket -Address 0x11001af4 -Bytes ([byte[]]::new(9)) } 'Oversized single ATT write'

$slots = [byte[]]::new(8 * 0x118)
[BitConverter]::GetBytes([uint32] 0x100122ed).CopyTo($slots, 3 * 0x118 + 0x10c)
[BitConverter]::GetBytes([uint32] 9).CopyTo($slots, 3 * 0x118 + 0x108)
[BitConverter]::GetBytes([uint32] 0x100178c0).CopyTo($slots, 3 * 0x118 + 0x104)
$slot = Get-Gmk70OtaCallbackSlot -Bytes $slots
Assert-Equal $slot ([uint32] 0x11006814) 'Live OTA slot selection'
[BitConverter]::GetBytes([uint32] 0x100122ed).CopyTo($slots, 4 * 0x118 + 0x10c)
Assert-Rejected { Get-Gmk70OtaCallbackSlot -Bytes $slots } 'Ambiguous OTA callback slot'
Write-Host 'PASS: RAM read/write encoding, exact scopes, destructive-opcode rejection, packet sizes and live-slot ambiguity.'
