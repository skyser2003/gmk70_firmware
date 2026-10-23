# Portable protocol/file operations; this file never accesses a physical device.
Set-StrictMode -Version Latest

function ConvertTo-Gmk70WinRtArray {
    param([object] $Value, [type] $ItemType)
    # PowerShell's COM adapter does not expose WinRT vector-view members.
    # Let the CLR enumerate the typed interface, preserving the array shape.
    $method = @([System.Linq.Enumerable].GetMethods() | Where-Object {
        $_.Name -eq 'ToArray' -and $_.IsGenericMethodDefinition -and
        $_.GetGenericArguments().Length -eq 1 -and $_.GetParameters().Length -eq 1
    })[0]
    $arguments = [object[]]::new(1)
    $arguments[0] = $Value
    return ,$method.MakeGenericMethod($ItemType).Invoke($null, $arguments)
}

function ConvertFrom-Gmk70WinRtBuffer {
    param([object] $Buffer)
    if ($null -eq $Buffer) { return ,([byte[]]::new(0)) }
    # Reflection lets the CLR marshal IBuffer; PowerShell cannot bind its COM
    # wrapper to FromBuffer's interface parameter or access its Length.
    $arguments = [object[]]::new(1)
    $arguments[0] = $Buffer
    $reader = [Windows.Storage.Streams.DataReader].GetMethod('FromBuffer').Invoke($null, $arguments)
    try {
        $bytes = [byte[]]::new([int] $reader.UnconsumedBufferLength)
        $reader.ReadBytes($bytes)
        return ,$bytes
    }
    finally { $reader.Dispose() }
}

function Get-Gmk70DesktopHost {
    param([string] $Edition, [string] $OperatingSystem, [string] $SystemRoot, [bool] $AlreadyRelaunched)
    if ($Edition -eq 'Desktop' -or $OperatingSystem -ne 'Windows_NT' -or $AlreadyRelaunched -or $SystemRoot -eq '') {
        return $null
    }
    $path = Join-Path $SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    return $null
}

function New-Gmk70ReadPacket {
    [CmdletBinding()]
    param([uint32] $Offset, [int] $Length, [uint32] $RegionLength, [ValidateSet(6, 8)] [int] $Opcode = 6)
    if ($Length -lt 1 -or $Length -gt 128) { throw 'Read length must be between 1 and 128 bytes.' }
    if ($Opcode -eq 8 -and ($RegionLength -ne 0x20000 -or ([uint64] $Offset + $Length) -gt 0x20000)) {
        throw 'Memory reads are restricted to the documented 128 KiB ROM window.'
    }
    if (([uint64] $Offset + $Length) -gt $RegionLength) { throw 'Read is outside the declared image region.' }
    # Six-byte payload: u32 storage offset or ROM address, u16 length, little endian.
    $packet = [byte[]]::new(9)
    $packet[0] = [byte] $Opcode
    $packet[1] = 6
    for ($i = 0; $i -lt 4; $i++) { $packet[3 + $i] = [byte] (($Offset -shr (8 * $i)) -band 255) }
    $packet[7] = [byte] ($Length -band 255)
    $packet[8] = [byte] (($Length -shr 8) -band 255)
    return ,$packet
}

function Assert-Gmk70ReadPacket {
    param([byte[]] $Packet)
    if ($null -eq $Packet -or $Packet.Length -ne 9 -or $Packet[1] -ne 6 -or $Packet[2] -ne 0 -or $Packet[0] -notin @(6, 8)) {
        throw 'Refusing a packet that is not a bounded nine-byte read request.'
    }
    $offset = [BitConverter]::ToUInt32($Packet, 3)
    $length = [int] [BitConverter]::ToUInt16($Packet, 7)
    if ($length -lt 11 -or $length -gt 128) { throw 'Deferred read length must be between 11 and 128 bytes.' }
    $limit = if ($Packet[0] -eq 8) { [uint32] 0x20000 } else { [uint32] 0x80000 }
    New-Gmk70ReadPacket -Opcode $Packet[0] -Offset $offset -Length $length -RegionLength $limit | Out-Null
}

function Get-Gmk70CapturePlan {
    param([switch] $RecoverySnapshot)
    if ($RecoverySnapshot) {
        # The flash window is based on stock's configured data addresses, not a
        # measured chip capacity. A matching snapshot does not certify recovery.
        return @(
            [pscustomobject] @{ name = 'radio-flash'; opcode = 6; length = [uint32] 0x80000 },
            [pscustomobject] @{ name = 'radio-rom'; opcode = 8; length = [uint32] 0x20000 }
        )
    }
    return [pscustomobject] @{ name = 'radio-region'; opcode = 6; length = [uint32] 0x25000 }
}

function Get-Gmk70Header {
    param([byte[]] $Bytes, [uint32] $ExpectedImageSize = 0x25000)
    if ($null -eq $Bytes -or $Bytes.Length -lt 8) { throw 'Boot header is shorter than eight bytes.' }
    $size = [BitConverter]::ToUInt32($Bytes, 4)
    if ($size -ne $ExpectedImageSize) {
        throw ('Header declares 0x{0:X} bytes; expected the captured 0x{1:X}-byte image region.' -f $size, $ExpectedImageSize)
    }
    $fields = [ordered]@{
        reserved = [BitConverter]::ToUInt32($Bytes, 0)
        image_size = $size
    }
    foreach ($field in @(
        @{ name = 'image_type'; offset = 8 },
        @{ name = 'entry'; offset = 12 },
        @{ name = 'memory_init'; offset = 16 },
        @{ name = 'stack_top'; offset = 20 },
        @{ name = 'firmware_version'; offset = 24 }
    )) {
        if ($Bytes.Length -ge ($field.offset + 4)) {
            $fields[$field.name] = [BitConverter]::ToUInt32($Bytes, $field.offset)
        }
    }
    return [pscustomobject] $fields
}

function Read-Gmk70OtaBlock {
    [CmdletBinding()]
    param(
        [uint32] $Offset, [int] $Length, [uint32] $RegionLength,
        [scriptblock] $WritePacket, [scriptblock] $ReadValue,
        [ValidateSet(6, 8)] [int] $Opcode = 6
    )
    # SDK reads of <=10 bytes are returned in notifications. Larger reads queue
    # their data for FF00; its read callback consumes that pending value once.
    if ($Length -lt 11) { throw 'FF00 polling requires at least 11 bytes per request.' }
    $packet = New-Gmk70ReadPacket -Offset $Offset -Length $Length -RegionLength $RegionLength -Opcode $Opcode
    & $WritePacket $packet | Out-Null
    [byte[]] $bytes = & $ReadValue
    $actual = if ($null -eq $bytes) { 0 } else { $bytes.Length }
    if ($actual -ne $Length) {
        throw ('Expected {0} bytes at storage offset 0x{1:X}, received {2}.' -f $Length, $Offset, $actual)
    }
    return ,$bytes
}

function Write-Gmk70Region {
    param(
        [string] $Path, [uint32] $Length, [int] $ChunkSize,
        [scriptblock] $ReadBlock, [scriptblock] $OnProgress
    )
    if ($ChunkSize -lt 11 -or $ChunkSize -gt 128 -or $Length -eq 0 -or ($Length % $ChunkSize) -ne 0) {
        throw 'The region length must be a positive multiple of an 11..128-byte chunk size.'
    }
    $partial = $Path + '.partial'
    if ((Test-Path -LiteralPath $Path) -or (Test-Path -LiteralPath $partial)) {
        throw 'A dump or partial file already exists; refusing to overwrite it.'
    }
    $stream = [IO.File]::Open($partial, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        for ([uint32] $offset = 0; $offset -lt $Length; $offset += $ChunkSize) {
            [byte[]] $bytes = & $ReadBlock $offset $ChunkSize
            $actual = if ($null -eq $bytes) { 0 } else { $bytes.Length }
            if ($actual -ne $ChunkSize) {
                throw ('Expected {0} bytes at storage offset 0x{1:X}, received {2}.' -f $ChunkSize, $offset, $actual)
            }
            $stream.Write($bytes, 0, $bytes.Length)
            if ($null -ne $OnProgress -and ((($offset + $ChunkSize) % 4096) -eq 0 -or ($offset + $ChunkSize) -eq $Length)) {
                & $OnProgress ($offset + $ChunkSize) $Length | Out-Null
            }
        }
        $stream.Flush()
    }
    finally { $stream.Dispose() }
    [IO.File]::Move($partial, $Path)
    return [pscustomobject]@{
        path = [IO.Path]::GetFileName($Path)
        length = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Confirm-Gmk70Dump {
    param([string] $First, [string] $Second, [uint32] $Length)
    if ((Get-Item -LiteralPath $First).Length -ne $Length -or (Get-Item -LiteralPath $Second).Length -ne $Length) {
        throw 'Dump length does not match the declared image region.'
    }
    $firstHash = (Get-FileHash -LiteralPath $First -Algorithm SHA256).Hash.ToLowerInvariant()
    $secondHash = (Get-FileHash -LiteralPath $Second -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($firstHash -ne $secondHash) { throw 'The two dump passes differ; this image is not verified.' }
    return $firstHash
}

function Export-Gmk70CaptureArchives {
    param([string] $Directory, [switch] $RecoverySnapshot)
    if (-not $RecoverySnapshot) {
        $archive = $Directory + '.zip'
        Compress-Archive -LiteralPath $Directory -DestinationPath $archive -ErrorAction Stop
        return [pscustomobject] @{ analysis = $archive; private = $null }
    }
    $privateArchive = $Directory + '-private.zip'
    Compress-Archive -LiteralPath $Directory -DestinationPath $privateArchive -ErrorAction Stop
    # Main flash contains Bluetooth bonding keys. Share only a fixed allowlist of
    # ROM and acquisition metadata; never select analysis files by exclusion.
    $analysisFiles = @()
    foreach ($name in @('report.json', 'dump.log', 'header-128.bin',
            'radio-rom-pass1.bin', 'radio-rom-pass2.bin',
            'radio-rom-pass1.bin.partial', 'radio-rom-pass2.bin.partial')) {
        $path = Join-Path $Directory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { $analysisFiles += $path }
    }
    if ($analysisFiles.Count -eq 0) { throw 'No analysis evidence exists to archive.' }
    $analysisArchive = $Directory + '-analysis.zip'
    Compress-Archive -LiteralPath $analysisFiles -DestinationPath $analysisArchive -ErrorAction Stop
    return [pscustomobject] @{ analysis = $analysisArchive; private = $privateArchive }
}
