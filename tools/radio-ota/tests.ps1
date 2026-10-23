# Hardware-free tests. Run with PowerShell 7 on Linux or PowerShell 5.1 on Windows.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:failed = 0
$script:passed = 0
$core = Join-Path $PSScriptRoot 'radio-core.ps1'
if (Test-Path $core) { . $core }

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Throws([scriptblock] $Action, [string] $Pattern) {
    $caught = $false
    try { & $Action | Out-Null }
    catch {
        $caught = $true
        Assert-True ($_.Exception.Message -like $Pattern) ("Wrong failure: {0}" -f $_.Exception.Message)
    }
    Assert-True $caught 'Expected rejection, but operation succeeded'
}
function Test-Case([string] $Name, [scriptblock] $Action) {
    try { & $Action; $script:passed++; Write-Output "PASS $Name" }
    catch { $script:failed++; Write-Output ("FAIL {0}: {1}" -f $Name, $_.Exception.Message) }
}
function Hex([byte[]] $Bytes) { return [BitConverter]::ToString($Bytes) }

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('gmk70-radio-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($temporary) | Out-Null
try {
    Test-Case 'PowerShell 7 on Windows selects the built-in Desktop host rather than PATH' {
        $root = Join-Path $temporary 'Windows root with spaces'
        $directory = Join-Path $root 'System32/WindowsPowerShell/v1.0'
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        $exe = Join-Path $directory 'powershell.exe'
        [IO.File]::WriteAllBytes($exe, [byte[]] (0))
        $selected = Get-Gmk70DesktopHost -Edition Core -OperatingSystem Windows_NT -SystemRoot $root -AlreadyRelaunched $false
        Assert-True ($selected -eq $exe) 'The built-in Windows host was not selected'
    }
    Test-Case 'Desktop, non-Windows, unavailable host, and reentry never relaunch' {
        Assert-True ($null -eq (Get-Gmk70DesktopHost -Edition Desktop -OperatingSystem Windows_NT -SystemRoot $temporary -AlreadyRelaunched $false)) 'Desktop attempted a relaunch'
        Assert-True ($null -eq (Get-Gmk70DesktopHost -Edition Core -OperatingSystem Linux -SystemRoot $temporary -AlreadyRelaunched $false)) 'Non-Windows attempted a relaunch'
        Assert-True ($null -eq (Get-Gmk70DesktopHost -Edition Core -OperatingSystem Windows_NT -SystemRoot $temporary -AlreadyRelaunched $false)) 'An unavailable executable was selected'
        $root = Join-Path $temporary 'Windows root with spaces'
        Assert-True ($null -eq (Get-Gmk70DesktopHost -Edition Core -OperatingSystem Windows_NT -SystemRoot $root -AlreadyRelaunched $true)) 'Reentry would relaunch indefinitely'
    }
    Test-Case 'read command matches the successful eight-byte hardware request' {
        Assert-True ($null -ne (Get-Command New-Gmk70ReadPacket -ErrorAction SilentlyContinue)) 'Read packet encoder is not implemented'
        $packet = New-Gmk70ReadPacket -Offset 0 -Length 8 -RegionLength 0x25000
        Assert-True ((Hex $packet) -eq '06-06-00-00-00-00-00-08-00') 'Incorrect READ_DATA wire bytes'
    }
    Test-Case 'nonzero read address and length are little endian' {
        $packet = New-Gmk70ReadPacket -Offset 0x12340 -Length 128 -RegionLength 0x25000
        Assert-True ((Hex $packet) -eq '06-06-00-40-23-01-00-80-00') 'Incorrect address or length byte order'
    }
    Test-Case 'ROM reads use READ_MEM, not storage reads or memory writes' {
        $packet = New-Gmk70ReadPacket -Opcode 8 -Offset 0x1FF80 -Length 128 -RegionLength 0x20000
        Assert-True ((Hex $packet) -eq '08-06-00-80-FF-01-00-80-00') 'Incorrect ROM read command'
        Assert-Throws { New-Gmk70ReadPacket -Opcode 7 -Offset 0 -Length 16 -RegionLength 0x20000 } '*'
        Assert-Throws { New-Gmk70ReadPacket -Opcode 8 -Offset 0x20000 -Length 16 -RegionLength 0x80000 } '*ROM*'
        Assert-Throws { New-Gmk70ReadPacket -Opcode 8 -Offset 0x10000000 -Length 16 -RegionLength 0x20000 } '*ROM*'
    }
    Test-Case 'the sender rejects destructive and out-of-scope packets' {
        Assert-True ($null -ne (Get-Command Assert-Gmk70ReadPacket -ErrorAction SilentlyContinue)) 'Read-only sender guard is missing'
        foreach ($opcode in @(0, 1, 2, 3, 4, 5, 7, 9)) {
            Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] ($opcode, 6, 0, 0, 0, 0, 0, 16, 0)) } '*read*'
        }
        Assert-Gmk70ReadPacket -Packet ([byte[]] (8, 6, 0, 0, 0, 0, 0, 16, 0))
        Assert-Gmk70ReadPacket -Packet ([byte[]] (6, 6, 0, 0, 0, 0, 0, 128, 0))
        Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] (8, 6, 0, 0xF8, 0xFF, 1, 0, 16, 0)) } '*ROM*'
        Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] (6, 6, 0, 0xF8, 0xFF, 7, 0, 16, 0)) } '*outside*'
        Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] (6, 6, 0, 0, 0, 0, 0, 0, 0)) } '*length*'
        Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] (6, 6, 0, 0, 0, 0, 0, 129, 0)) } '*length*'
        Assert-Throws { Assert-Gmk70ReadPacket -Packet ([byte[]] (6, 6, 1, 0, 0, 0, 0, 16, 0)) } '*read*'
    }
    Test-Case 'normal capture remains application-only; recovery adds bounded ROM and flash snapshots' {
        $normal = @(Get-Gmk70CapturePlan)
        Assert-True ($normal.Count -eq 1 -and $normal[0].name -eq 'radio-region' -and $normal[0].length -eq 0x25000 -and $normal[0].opcode -eq 6) 'Default acquisition changed'
        $recovery = @(Get-Gmk70CapturePlan -RecoverySnapshot)
        Assert-True ($recovery.Count -eq 2) 'Recovery capture regions are missing'
        Assert-True ($recovery[0].name -eq 'radio-flash' -and $recovery[0].length -eq 0x80000 -and $recovery[0].opcode -eq 6) 'The extended storage window is wrong'
        Assert-True ($recovery[1].name -eq 'radio-rom' -and $recovery[1].length -eq 0x20000 -and $recovery[1].opcode -eq 8) 'The ROM window is wrong'
    }
    Test-Case 'ROM polling forwards the memory opcode and consumes its deferred value exactly once' {
        $state = @{ packet = $null; reads = 0 }
        $write = { param($Packet) $state.packet = $Packet }.GetNewClosure()
        $read = { $state.reads++; return ,([byte[]] (0..15)) }.GetNewClosure()
        $bytes = Read-Gmk70OtaBlock -Opcode 8 -Offset 0x20 -Length 16 -RegionLength 0x20000 -WritePacket $write -ReadValue $read
        Assert-True ((Hex $state.packet) -eq '08-06-00-20-00-00-00-10-00') 'ROM request used the storage opcode'
        Assert-True ($state.reads -eq 1 -and (Hex $bytes) -eq (Hex ([byte[]] (0..15)))) 'Deferred ROM value was lost or read twice'
    }
    Test-Case 'recovery analysis ZIP excludes flash snapshots and partial private data' {
        $folder = Join-Path $temporary 'recovery-package'
        [IO.Directory]::CreateDirectory($folder) | Out-Null
        foreach ($name in @('report.json', 'dump.log', 'header-128.bin', 'radio-rom-pass1.bin', 'radio-rom-pass2.bin')) {
            [IO.File]::WriteAllText((Join-Path $folder $name), 'analysis evidence')
        }
        foreach ($name in @('radio-flash-pass1.bin', 'radio-flash-pass2.bin.partial', 'unexpected-private.bin')) {
            [IO.File]::WriteAllText((Join-Path $folder $name), 'private pairing data')
        }
        $archives = Export-Gmk70CaptureArchives -Directory $folder -RecoverySnapshot
        Assert-True (Test-Path -LiteralPath $archives.private) 'Private backup ZIP is missing'
        Assert-True (Test-Path -LiteralPath $archives.analysis) 'Analysis ZIP is missing'
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($archives.analysis)
        try {
            $names = @($zip.Entries | ForEach-Object { $_.Name } | Sort-Object)
            Assert-True (($names -join ',') -eq 'dump.log,header-128.bin,radio-rom-pass1.bin,radio-rom-pass2.bin,report.json') 'Analysis archive includes private flash data or omits evidence'
        }
        finally { $zip.Dispose() }
    }
    Test-Case 'read request cannot exceed the declared region' {
        Assert-Throws { New-Gmk70ReadPacket -Offset 0x24FF8 -Length 16 -RegionLength 0x25000 } '*outside*'
        Assert-Throws { New-Gmk70ReadPacket -Offset 0 -Length 0 -RegionLength 0x25000 } '*length*'
    }
    Test-Case 'captured boot header declares 148 KiB, without claiming chip capacity' {
        $header = [byte[]] (0, 0, 0, 0, 0, 0x50, 2, 0)
        $metadata = Get-Gmk70Header -Bytes $header
        Assert-True ($metadata.image_size -eq 151552) 'Wrong declared image size'
        Assert-True ($metadata.reserved -eq 0) 'Wrong reserved field'
        Assert-Throws { Get-Gmk70Header -Bytes ([byte[]] (0, 0)) } '*header*'
        Assert-Throws { Get-Gmk70Header -Bytes ([byte[]] (0, 0, 0, 0, 0, 0x60, 2, 0)) } '*expected*'
    }
    Test-Case 'polling requests use READ_DATA and consume the FF00 value' {
        $state = @{ packet = $null; reads = 0 }
        $write = { param([byte[]] $Packet) $state.packet = $Packet }.GetNewClosure()
        $read = { $state.reads++; return ,([byte[]] (0..15)) }.GetNewClosure()
        $block = Read-Gmk70OtaBlock -Offset 32 -Length 16 -RegionLength 128 -WritePacket $write -ReadValue $read
        Assert-True ((Hex $state.packet) -eq '06-06-00-20-00-00-00-10-00') 'Wrong polling command'
        Assert-True ((Hex $block) -eq '00-01-02-03-04-05-06-07-08-09-0A-0B-0C-0D-0E-0F') 'Polling data changed'
        Assert-True ($state.reads -eq 1) 'Expected one FF00 consumption per request'
    }
    Test-Case 'polling rejects empty, short, and oversized values' {
        $write = { param($Packet) }
        foreach ($count in @(0, 15, 17)) {
            $read = { return ,([byte[]]::new($count)) }.GetNewClosure()
            Assert-Throws { Read-Gmk70OtaBlock -Offset 0 -Length 16 -RegionLength 128 -WritePacket $write -ReadValue $read } '*Expected 16*'
        }
        Assert-Throws { Read-Gmk70OtaBlock -Offset 0 -Length 8 -RegionLength 128 -WritePacket $write -ReadValue { } } '*11*'
    }
    Test-Case 'GATT write failure prevents an unrelated read' {
        $state = @{ reads = 0 }
        $read = { $state.reads++; return ,([byte[]]::new(16)) }.GetNewClosure()
        Assert-Throws { Read-Gmk70OtaBlock -Offset 0 -Length 16 -RegionLength 128 -WritePacket { throw 'GATT rejected' } -ReadValue $read } '*GATT rejected*'
        Assert-True ($state.reads -eq 0) 'FF00 read occurred despite failed request'
    }
    Test-Case 'dump writes offsets once in order and retains zero bytes' {
        $state = @{ offsets = [Collections.Generic.List[int]]::new() }
        $read = {
            param([uint32] $Offset, [int] $Length)
            $state.offsets.Add([int] $Offset)
            $bytes = [byte[]]::new($Length)
            for ($i = 0; $i -lt $Length; $i++) { $bytes[$i] = [byte] ($Offset + $i) }
            return ,$bytes
        }.GetNewClosure()
        $file = Join-Path $temporary 'ordered.bin'
        $result = Write-Gmk70Region -Path $file -Length 48 -ChunkSize 16 -ReadBlock $read
        Assert-True ((Hex ([IO.File]::ReadAllBytes($file))) -eq (Hex ([byte[]] (0..47)))) 'Dump is not the requested contiguous region'
        Assert-True (($state.offsets -join ',') -eq '0,16,32') 'Dump skipped/repeated offsets'
        Assert-True ($result.length -eq 48) 'Incorrect completed length'
    }
    Test-Case 'short block leaves only a partial file' {
        $file = Join-Path $temporary 'short.bin'
        Assert-Throws { Write-Gmk70Region -Path $file -Length 32 -ChunkSize 16 -ReadBlock { return ,([byte[]]::new(15)) } } '*Expected 16*'
        Assert-True (-not (Test-Path $file)) 'Incomplete image was promoted to complete'
        Assert-True (Test-Path ($file + '.partial')) 'Partial evidence was discarded'
    }
    Test-Case 'a disconnect preserves completed partial bytes and never completes the image' {
        $file = Join-Path $temporary 'disconnect.bin'
        $read = {
            param($Offset, $Length)
            if ($Offset -gt 0) { throw 'Radio disconnected' }
            return ,([byte[]] (0..15))
        }
        Assert-Throws { Write-Gmk70Region -Path $file -Length 32 -ChunkSize 16 -ReadBlock $read } '*Radio disconnected*'
        Assert-True (-not (Test-Path $file)) 'Disconnected acquisition became a complete file'
        Assert-True ((Hex ([IO.File]::ReadAllBytes($file + '.partial'))) -eq (Hex ([byte[]] (0..15)))) 'Completed partial bytes were lost'
    }
    Test-Case 'an existing dump is never overwritten' {
        $file = Join-Path $temporary 'existing.bin'
        [IO.File]::WriteAllBytes($file, [byte[]] (1, 2, 3))
        Assert-Throws { Write-Gmk70Region -Path $file -Length 16 -ChunkSize 16 -ReadBlock { return ,([byte[]]::new(16)) } } '*exist*'
        Assert-True ((Hex ([IO.File]::ReadAllBytes($file))) -eq '01-02-03') 'Existing dump was overwritten'
    }
    Test-Case 'two complete identical passes produce a verified digest' {
        $a = Join-Path $temporary 'a.bin'; $b = Join-Path $temporary 'b.bin'
        [IO.File]::WriteAllBytes($a, [byte[]] (0..31)); [IO.File]::WriteAllBytes($b, [byte[]] (0..31))
        $hash = Confirm-Gmk70Dump -First $a -Second $b -Length 32
        Assert-True ($hash -eq (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash.ToLowerInvariant()) 'Incorrect verified digest'
        [IO.File]::WriteAllBytes($b, [byte[]] (1..32))
        Assert-Throws { Confirm-Gmk70Dump -First $a -Second $b -Length 32 } '*differ*'
        [IO.File]::WriteAllBytes($b, [byte[]] (0..15))
        Assert-Throws { Confirm-Gmk70Dump -First $a -Second $b -Length 32 } '*length*'
    }
    Test-Case 'two full 148 KiB acquisitions reproduce a reference image exactly' {
        $reference = [byte[]]::new(0x25000)
        [Random]::new(70).NextBytes($reference)
        [Array]::Copy([byte[]] (0, 0, 0, 0, 0, 0x50, 2, 0), $reference, 8)
        $read = {
            param($Offset, $Length)
            $bytes = [byte[]]::new($Length)
            [Array]::Copy($reference, [int] $Offset, $bytes, 0, $Length)
            return ,$bytes
        }.GetNewClosure()
        $a = Join-Path $temporary 'full-a.bin'; $b = Join-Path $temporary 'full-b.bin'
        Write-Gmk70Region -Path $a -Length 0x25000 -ChunkSize 128 -ReadBlock $read | Out-Null
        Write-Gmk70Region -Path $b -Length 0x25000 -ChunkSize 128 -ReadBlock $read | Out-Null
        Confirm-Gmk70Dump -First $a -Second $b -Length 0x25000 | Out-Null
        Assert-True ([Convert]::ToBase64String([IO.File]::ReadAllBytes($a)) -eq [Convert]::ToBase64String($reference)) 'A repeated acquisition error passed the two-pass comparison'
    }
}
finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
Write-Output ("{0} passed; {1} failed" -f $script:passed, $script:failed)
if ($script:failed -ne 0) { exit 1 }
