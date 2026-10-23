#requires -Version 5.1
<#
Read-only GMK70 radio acquisition using built-in Windows WinRT APIs.
Normal mode uses OTA opcode 6 (READ_DATA). RecoverySnapshot also uses opcode 8
(READ_MEM), restricted to ROM. No erase, programming, RAM-write, reboot,
host-switch, pairing, or keyboard settings commands are implemented here.
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$')]
    [string] $BluetoothAddress = 'c7:02:53:aa:76:69',
    [string] $OutputDirectory = '',
    [ValidateRange(5, 60)]
    [int] $TimeoutSeconds = 20,
    [switch] $SmallChunks,
    [switch] $RecoverySnapshot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'radio-core.ps1')

# The WinRT projections used below require Desktop .NET. Select that host before
# creating any output files, preserving arguments and avoiding relaunch loops.
$desktopHost = Get-Gmk70DesktopHost -Edition $PSVersionTable.PSEdition -OperatingSystem $env:OS `
    -SystemRoot $env:SystemRoot -AlreadyRelaunched ($env:GMK70_RADIO_DESKTOP_HOST -eq '1')
if ($null -ne $desktopHost) {
    Write-Host ('Launching built-in Windows PowerShell 5.1: {0}' -f $desktopHost)
    $desktopArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
        '-BluetoothAddress', $BluetoothAddress, '-TimeoutSeconds', [string] $TimeoutSeconds)
    if ($OutputDirectory -ne '') { $desktopArguments += @('-OutputDirectory', $OutputDirectory) }
    if ($SmallChunks) { $desktopArguments += '-SmallChunks' }
    if ($RecoverySnapshot) { $desktopArguments += '-RecoverySnapshot' }
    $previousHostMarker = $env:GMK70_RADIO_DESKTOP_HOST
    try {
        $env:GMK70_RADIO_DESKTOP_HOST = '1'
        & $desktopHost @desktopArguments
        $desktopExitCode = $LASTEXITCODE
    }
    finally {
        if ($null -eq $previousHostMarker) { Remove-Item Env:GMK70_RADIO_DESKTOP_HOST -ErrorAction SilentlyContinue }
        else { $env:GMK70_RADIO_DESKTOP_HOST = $previousHostMarker }
    }
    exit $desktopExitCode
}

if ($OutputDirectory -eq '') {
    $prefix = if ($RecoverySnapshot) { 'gmk70-radio-recovery-' } else { 'gmk70-radio-dump-' }
    $OutputDirectory = Join-Path $PSScriptRoot ($prefix + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; choose a new directory.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$script:logPath = Join-Path $OutputDirectory 'dump.log'
$script:utf8 = [Text.UTF8Encoding]::new($false)
$script:stage = 'Initialization'
$script:asTaskMethod = $null
$script:writeRequests = 0
$script:readResponses = 0
$script:currentOffset = 0
$script:currentPass = 0
$capturePlan = @(Get-Gmk70CapturePlan -RecoverySnapshot:$RecoverySnapshot)
$script:readOpcode = 6
$script:readLimit = [uint32] $capturePlan[0].length
$script:currentRegion = [string] $capturePlan[0].name
$device = $null
$service = $null
$commandCharacteristic = $null
$dataCharacteristic = $null
$failed = $true
$declaredSize = [uint32] 0x25000
$report = [ordered]@{
    tool = 'GMK70 radio region dumper'; version = 3; local_revision = 'winrt-projections-1'
    started_utc = [DateTime]::UtcNow.ToString('o')
    status = 'incomplete'; verified = $false; complete_recovery_backup = $false
    bluetooth_address = $BluetoothAddress
    scope = @{ start_offset = 0; length = $script:readLimit; application_image_size = $declaredSize; total_flash_capacity_known = $false; active_bank_known = $false }
    recovery_snapshot = [bool] $RecoverySnapshot
    rollback_verified = $false
    transport = 'WinRT FF01 read commands with GATT write responses; FF00 uncached polling'
    ota_opcodes = @(6)
    ota_response_notifications_captured = $false
    preflight = @(); passes = @(); regions = @()
    limitations = @(
        'Normal mode captures only the declared application region; recovery mode adds a candidate 512 KiB flash window and 128 KiB ROM.',
        'The flash window is based on stock configuration, not measured capacity. OTP/calibration storage is separate and is not read.',
        'No restore, ROM programmer connection, or USB radio recovery has been tested. This acquisition does not certify rollback.',
        'GATT write status and exact read lengths checked; no OTA notification acknowledgements are captured.',
        'Matching passes establish repeatability; they do not independently prove physical flash mapping or patchability.'
    )
    source_sha256 = @{}
}

function Write-DumpLog([string] $Message) {
    Write-Host $Message
    [IO.File]::AppendAllText($script:logPath, $Message + [Environment]::NewLine, $script:utf8)
}

function Wait-Gmk70WinRt {
    param([scriptblock] $StartOperation, [type] $ResultType, [string] $Stage)
    $script:stage = $Stage
    $operation = & $StartOperation
    $task = $script:asTaskMethod.MakeGenericMethod($ResultType).Invoke($null, @($operation))
    if (-not $task.Wait($TimeoutSeconds * 1000)) {
        try { $operation.Cancel() } catch { }
        # FF00 consumes a pending response. Never start a fallback request after
        # timeout/cancellation while the previous WinRT operation may still run.
        throw [TimeoutException]::new("Timed out after $TimeoutSeconds seconds during $Stage")
    }
    return ,$task.Result
}

function Assert-GattSuccess([object] $Result, [string] $Label) {
    if ($Result.Status.ToString() -ne 'Success') {
        $message = '{0}: Status={1}' -f $Label, $Result.Status
        if ($null -ne $Result.ProtocolError) { $message += ' ATT=0x{0:X2}' -f [int] $Result.ProtocolError }
        throw $message
    }
}

function Send-Gmk70ReadPacket([byte[]] $Packet) {
    Assert-Gmk70ReadPacket -Packet $Packet
    $writer = [Windows.Storage.Streams.DataWriter]::new()
    try {
        $writer.WriteBytes($Packet)
        $buffer = $writer.DetachBuffer()
        $writeMethod = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic].GetMethod(
            'WriteValueWithResultAsync', [type[]] @([Windows.Storage.Streams.IBuffer],
                [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]))
        $writeArguments = [object[]] @($buffer,
            [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]::WriteWithResponse)
        $script:writeRequests++
        $result = Wait-Gmk70WinRt {
            $writeMethod.Invoke($commandCharacteristic, $writeArguments)
        } ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteResult]) ('Read opcode {0}, {1} at 0x{2:X}' -f $Packet[0], $script:currentRegion, $script:currentOffset)
        Assert-GattSuccess $result 'Read request GATT write'
    }
    finally { $writer.Dispose() }
}

function Receive-Gmk70ReadValue {
    $result = Wait-Gmk70WinRt { $dataCharacteristic.ReadValueAsync($uncached) } `
        ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult]) ('Read FF00 at 0x{0:X}' -f $script:currentOffset)
    Assert-GattSuccess $result 'FF00 GATT read'
    $script:readResponses++
    return ,(ConvertFrom-Gmk70WinRtBuffer -Buffer $result.Value)
}

try {
    Write-DumpLog 'GMK70 radio dumper v3: bounded read-only acquisition; no radio erase, programming or explicit reset.'
    if ($RecoverySnapshot) {
        Write-DumpLog 'Recovery evidence: candidate flash [0, 0x80000) and ROM [0, 0x20000), twice each. Capacity and rollback remain unverified.'
    }
    Write-DumpLog ('PowerShell {0}; OS {1}' -f $PSVersionTable.PSVersion, [Environment]::OSVersion.VersionString)
    if ($PSVersionTable.PSEdition -ne 'Desktop') {
        throw 'Windows PowerShell 5.1 is required for WinRT. Automatic launch was unavailable or did not select a Desktop host.'
    }
    $report.source_sha256['dump-radio.ps1'] = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $report.source_sha256['radio-core.ps1'] = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'radio-core.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()

    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.BluetoothCacheMode, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Enumeration.DeviceAccessStatus, Windows.Devices.Enumeration, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataWriter, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.IBuffer, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null

    $methods = @([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and
        $_.GetGenericArguments().Length -eq 1 -and $_.GetParameters().Length -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
    })
    if ($methods.Count -ne 1) { throw 'Cannot find the built-in WinRT AsTask bridge.' }
    $script:asTaskMethod = $methods[0]

    Write-DumpLog ('Connecting to {0}; keep the keyboard in Bluetooth mode.' -f $BluetoothAddress)
    $address = [Convert]::ToUInt64($BluetoothAddress.Replace(':', ''), 16)
    $device = Wait-Gmk70WinRt { [Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address) } `
        ([Windows.Devices.Bluetooth.BluetoothLEDevice]) 'Open Bluetooth device'
    if ($null -eq $device) { throw 'Windows could not open the device. Confirm GMK70-2 is awake and connected over Bluetooth.' }
    $report['device_name'] = $device.Name
    Write-DumpLog ('Device: {0}; connection: {1}' -f $device.Name, $device.ConnectionStatus)
    $uncached = [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached
    $uuid = [Guid] '02f00000-0000-0000-0000-00000000fe00'
    $services = Wait-Gmk70WinRt { $device.GetGattServicesForUuidAsync($uuid, $uncached) } `
        ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) 'Discover OTA service'
    Assert-GattSuccess $services 'Service discovery'
    $serviceItems = ConvertTo-Gmk70WinRtArray -Value $services.Services `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService])
    if ($serviceItems.Length -ne 1) { throw 'Expected one matching OTA service.' }
    $service = $serviceItems[0]
    $access = Wait-Gmk70WinRt { $service.RequestAccessAsync() } `
        ([Windows.Devices.Enumeration.DeviceAccessStatus]) 'Request service access'
    if ($access.ToString() -ne 'Allowed') { throw ('Service access: {0}' -f $access) }
    $chars = Wait-Gmk70WinRt { $service.GetCharacteristicsAsync($uncached) } `
        ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult]) 'Discover OTA characteristics'
    Assert-GattSuccess $chars 'Characteristic discovery'
    $report['characteristics'] = @()
    $characteristicItems = ConvertTo-Gmk70WinRtArray -Value $chars.Characteristics `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic])
    foreach ($char in $characteristicItems) {
        $report.characteristics += @{ uuid = $char.Uuid.ToString(); handle = $char.AttributeHandle; properties = $char.CharacteristicProperties.ToString() }
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff01') { $commandCharacteristic = $char }
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff00') { $dataCharacteristic = $char }
    }
    if ($null -eq $commandCharacteristic -or $null -eq $dataCharacteristic) { throw 'FF01 or FF00 is missing.' }
    $readBlock = {
        param([uint32] $Offset, [int] $Count)
        $script:currentOffset = $Offset
        return ,(Read-Gmk70OtaBlock -Opcode $script:readOpcode -Offset $Offset -Length $Count -RegionLength $script:readLimit `
            -WritePacket { param($Packet) Send-Gmk70ReadPacket $Packet } -ReadValue { Receive-Gmk70ReadValue })
    }

    Write-DumpLog 'Preflight: reading 16 bytes through FF00 and checking the captured boot header.'
    [byte[]] $anchor = & $readBlock 0 16
    $report['header'] = Get-Gmk70Header -Bytes $anchor
    if ($report.header.reserved -ne 0) { throw 'The header differs from the captured GMK70 header.' }
    $report.preflight += @{ length = 16; status = 'success'; hex = [BitConverter]::ToString($anchor) }
    $chunkSize = 16
    if (-not $SmallChunks) {
        # A 128-byte read may need ATT long-read support. Only use it when the
        # complete value is returned and its first 16 bytes agree with the anchor.
        $candidate = $null
        try {
            [byte[]] $candidate = & $readBlock 0 128
            $report.preflight += @{ length = 128; status = 'success' }
        }
        catch {
            if ($_.Exception -is [TimeoutException]) { throw }
            $report.preflight += @{ length = 128; status = 'failed'; error = $_.Exception.Message }
            Write-DumpLog ('128-byte preflight failed; using 16-byte reads: {0}' -f $_.Exception.Message)
            [byte[]] $recheck = & $readBlock 0 16
            if ([BitConverter]::ToString($recheck) -ne [BitConverter]::ToString($anchor)) { throw 'The preflight header changed.' }
        }
        if ($null -ne $candidate) {
            if ([BitConverter]::ToString($candidate, 0, 16) -ne [BitConverter]::ToString($anchor)) { throw 'The large-read header differs from the small-read header.' }
            $chunkSize = 128
        }
    }
    $report['chunk_size'] = $chunkSize
    $progress = {
        param($Done, $Total)
        Write-DumpLog ('{0} pass {1}/2: {2}/{3} bytes ({4:P0})' -f $script:currentRegion, $script:currentPass, $Done, $Total, ($Done / [double] $Total))
    }
    foreach ($region in $capturePlan) {
        $script:currentRegion = [string] $region.name
        $script:readOpcode = [int] $region.opcode
        $script:readLimit = [uint32] $region.length
        $script:currentPass = 0
        $report.ota_opcodes = @($report.ota_opcodes + $script:readOpcode | Select-Object -Unique)
        $entry = [ordered]@{ name = $region.name; opcode = $region.opcode; start = 0; length = $region.length; verified = $false; passes = @() }
        $report.regions += $entry
        $first = Join-Path $OutputDirectory ($region.name + '-pass1.bin')
        $second = Join-Path $OutputDirectory ($region.name + '-pass2.bin')
        foreach ($path in @($first, $second)) {
            $script:currentPass++
            $entry.passes += Write-Gmk70Region -Path $path -Length $region.length -ChunkSize $chunkSize -ReadBlock $readBlock -OnProgress $progress
        }
        $entry['sha256'] = Confirm-Gmk70Dump -First $first -Second $second -Length $region.length
        $entry.verified = $true
        Write-DumpLog ('Both {0} passes match. SHA256: {1}' -f $region.name, $entry.sha256)
        if ($region.opcode -eq 6) {
            $report.passes = $entry.passes
            $report['sha256'] = $entry.sha256
            $bytes = [IO.File]::ReadAllBytes($first)
            if ([BitConverter]::ToString($bytes, 0, 16) -ne [BitConverter]::ToString($anchor)) { throw 'The dump header differs from the preflight header.' }
            $report.header = Get-Gmk70Header -Bytes $bytes
            $digest = [Security.Cryptography.SHA256]::Create()
            try {
                $applicationHash = [BitConverter]::ToString($digest.ComputeHash($bytes, 0, [int] $declaredSize)).Replace('-', '').ToLowerInvariant()
            }
            finally { $digest.Dispose() }
            $report['application_region_sha256'] = $applicationHash
            $report['matches_reference_stock_application'] = $applicationHash -eq '100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c'
            [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'header-128.bin'), [byte[]] $bytes[0..127])
            $report['header_hex_128'] = [BitConverter]::ToString($bytes, 0, 128)
        }
    }
    $report.status = 'verified_region'; $report.verified = $true; $failed = $false
    Write-DumpLog 'Requested snapshots are repeatable. This does not establish a working restoration procedure.'
}
catch {
    $failed = $true
    $report.status = 'failed'; $report.verified = $false
    $errors = @('Failure during: ' + $script:stage)
    $exception = $_.Exception
    while ($null -ne $exception) {
        $errors += '{0} HRESULT=0x{1}: {2}' -f $exception.GetType().FullName, $exception.HResult.ToString('X8'), $exception.Message
        $exception = $exception.InnerException
    }
    $report['errors'] = $errors
    $report['failure_location'] = $_.InvocationInfo.PositionMessage
    $report['script_stack_trace'] = $_.ScriptStackTrace
    foreach ($line in $errors) { Write-DumpLog $line }
}
finally {
    if ($null -ne $service) { try { $service.Dispose() } catch { Write-DumpLog ('Service cleanup: ' + $_.Exception.Message) } }
    if ($null -ne $device) { try { $device.Dispose() } catch { Write-DumpLog ('Device cleanup: ' + $_.Exception.Message) } }
    $report['finished_utc'] = [DateTime]::UtcNow.ToString('o')
    $report['read_requests_attempted'] = $script:writeRequests
    $report['gatt_read_responses'] = $script:readResponses
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'report.json'), ($report | ConvertTo-Json -Depth 10), $script:utf8)
    try {
        $archives = Export-Gmk70CaptureArchives -Directory $OutputDirectory -RecoverySnapshot:$RecoverySnapshot
        if ($null -ne $archives.private) {
            Write-Host ('Keep this backup PRIVATE on your PC; it includes pairing data: {0}' -f $archives.private)
        }
        Write-Host ('Send this analysis file: {0}' -f $archives.analysis)
    }
    catch {
        $failed = $true
        if ($RecoverySnapshot) {
            Write-Host ('Could not create ZIP: {0}. Keep the output folder private: {1}' -f $_.Exception.Message, $OutputDirectory)
            Write-Host 'Share only report.json, dump.log, header-128.bin and radio-rom files; keep radio-flash files private.'
        }
        else {
            Write-Host ('Could not create ZIP: {0}. Send the output folder: {1}' -f $_.Exception.Message, $OutputDirectory)
        }
    }
}
if ($failed) { exit 1 }
