# Pure packet validation. Loading this file never opens a physical device.
#requires -Version 5.1
Set-StrictMode -Version Latest

function Assert-Gmk70RamRange([uint32] $Address, [int] $Length) {
    if ($Length -le 0 -or $Address -lt 0x11000000 -or ([uint64] $Address + $Length) -gt 0x1100e000) {
        throw 'Operation is outside the documented radio RAM window.'
    }
}

function New-Gmk70RamReadPacket {
    param([uint32] $Address, [int] $Length)
    Assert-Gmk70RamRange $Address $Length
    if ($Length -lt 11 -or $Length -gt 128) { throw 'Deferred reads require 11..128 bytes.' }
    $packet = [byte[]]::new(9)
    $packet[0] = 8
    $packet[1] = 6
    [BitConverter]::GetBytes($Address).CopyTo($packet, 3)
    [BitConverter]::GetBytes([uint16] $Length).CopyTo($packet, 7)
    return ,$packet
}

function New-Gmk70RamWritePacket {
    param([uint32] $Address, [byte[]] $Bytes)
    if ($null -eq $Bytes -or $Bytes.Length -lt 1 -or $Bytes.Length -gt 8) {
        throw 'A RAM write must contain 1..8 bytes and fit one minimum-MTU ATT request.'
    }
    Assert-Gmk70RamRange $Address $Bytes.Length
    $packet = [byte[]]::new(9 + $Bytes.Length)
    $packet[0] = 7
    $packet[1] = [byte] (6 + $Bytes.Length)
    [BitConverter]::GetBytes($Address).CopyTo($packet, 3)
    [BitConverter]::GetBytes([uint16] $Bytes.Length).CopyTo($packet, 7)
    $Bytes.CopyTo($packet, 9)
    return ,$packet
}

function Assert-Gmk70TrialPacket {
    param([byte[]] $Packet, [object[]] $WriteRanges = @())
    if ($null -eq $Packet -or $Packet.Length -lt 9 -or $Packet[2] -ne 0) {
        throw 'Malformed trial packet.'
    }
    $address = [BitConverter]::ToUInt32($Packet, 3)
    $length = [int] [BitConverter]::ToUInt16($Packet, 7)
    switch ($Packet[0]) {
        6 {
            if ($Packet.Length -ne 9 -or $Packet[1] -ne 6 -or $length -lt 11 -or
                $length -gt 128 -or ([uint64] $address + $length) -gt 0x80000) {
                throw 'Invalid bounded storage read.'
            }
        }
        8 {
            if ($Packet.Length -ne 9 -or $Packet[1] -ne 6) { throw 'Invalid memory read.' }
            New-Gmk70RamReadPacket -Address $address -Length $length | Out-Null
        }
        7 {
            if ($length -lt 1 -or $length -gt 8 -or $Packet.Length -ne (9 + $length) -or
                $Packet[1] -ne (6 + $length)) { throw 'Invalid single-request RAM write.' }
            Assert-Gmk70RamRange $address $length
            $allowed = $false
            foreach ($range in $WriteRanges) {
                Assert-Gmk70RamRange ([uint32] $range.start) ([int] $range.length)
                if ($address -ge [uint32] $range.start -and
                    ([uint64] $address + $length) -le ([uint64] $range.start + [uint32] $range.length)) {
                    $allowed = $true
                }
            }
            if (-not $allowed) { throw 'RAM write is outside the verified live scope.' }
        }
        default { throw 'Only storage reads, RAM reads and scoped RAM writes are permitted.' }
    }
}

function Get-Gmk70OtaCallbackSlot {
    param([byte[]] $Bytes)
    if ($Bytes.Length -ne (8 * 0x118)) { throw 'Incomplete GATT slot snapshot.' }
    $matches = @()
    for ($index = 0; $index -lt 8; $index++) {
        $offset = $index * 0x118
        if ([BitConverter]::ToUInt32($Bytes, $offset + 0x10c) -eq 0x100122ed) { $matches += $offset }
    }
    if ($matches.Length -ne 1) { throw 'Expected exactly one live stock OTA callback.' }
    $offset = [int] $matches[0]
    $table = [BitConverter]::ToUInt32($Bytes, $offset + 0x104)
    $count = [int] $Bytes[$offset + 0x108]
    if ($table -lt 0x10002000 -or $table -ge 0x10025000 -or $count -lt 1 -or $count -gt 64) {
        throw 'OTA slot does not reference a bounded stock attribute table.'
    }
    return [uint32] (0x110063c0 + $offset + 0x10c)
}
