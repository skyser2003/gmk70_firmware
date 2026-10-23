# Explicit physical-device transport. Importing it does not connect or send.
#requires -Version 5.1
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'radio-core.ps1')
. (Join-Path $PSScriptRoot 'ram-trial-core.ps1')
$script:trialDevice = $null
$script:trialService = $null
$script:trialCommand = $null
$script:trialData = $null
$script:trialTaskBridge = $null
$script:trialWriteRanges = @()
$script:trialSent = [Collections.Generic.List[object]]::new()

function Wait-Gmk70TrialOperation {
    param([scriptblock] $Start, [type] $ResultType, [string] $Label)
    $operation = & $Start
    $task = $script:trialTaskBridge.MakeGenericMethod($ResultType).Invoke($null, @($operation))
    if (-not $task.Wait(20000)) {
        try { $operation.Cancel() } catch { }
        throw [TimeoutException]::new("Timed out during $Label; no retry or additional write is permitted.")
    }
    return ,$task.Result
}

function Assert-Gmk70TrialGatt([object] $Result, [string] $Label) {
    if ($Result.Status.ToString() -ne 'Success') { throw "$Label returned $($Result.Status)." }
}

function Open-Gmk70TrialRadio {
    param([string] $BluetoothAddress = 'c7:02:53:aa:76:69')
    if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Use Windows PowerShell 5.1.' }
    if ($BluetoothAddress -ne 'c7:02:53:aa:76:69') { throw 'This trial is scoped to the verified GMK70-2 radio.' }
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
    if ($methods.Length -ne 1) { throw 'Cannot find the WinRT task bridge.' }
    $script:trialTaskBridge = $methods[0]
    $address = [Convert]::ToUInt64($BluetoothAddress.Replace(':', ''), 16)
    $script:trialDevice = Wait-Gmk70TrialOperation {
        [Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address)
    } ([Windows.Devices.Bluetooth.BluetoothLEDevice]) 'open radio'
    if ($null -eq $script:trialDevice -or $script:trialDevice.Name -ne 'GMK70-2') { throw 'Expected GMK70-2.' }
    Write-Host ('GMK70-2 Bluetooth connection: {0}' -f $script:trialDevice.ConnectionStatus)
    $uncached = [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached
    $uuid = [Guid] '02f00000-0000-0000-0000-00000000fe00'
    $result = Wait-Gmk70TrialOperation { $script:trialDevice.GetGattServicesForUuidAsync($uuid, $uncached) } `
        ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) 'discover OTA service'
    Assert-Gmk70TrialGatt $result 'Service discovery'
    $services = ConvertTo-Gmk70WinRtArray -Value $result.Services `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService])
    if ($services.Length -ne 1) { throw 'Expected exactly one OTA service.' }
    $script:trialService = $services[0]
    $access = Wait-Gmk70TrialOperation { $script:trialService.RequestAccessAsync() } `
        ([Windows.Devices.Enumeration.DeviceAccessStatus]) 'service access'
    if ($access.ToString() -ne 'Allowed') { throw 'OTA service access denied.' }
    $result = Wait-Gmk70TrialOperation { $script:trialService.GetCharacteristicsAsync($uncached) } `
        ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult]) 'characteristics'
    Assert-Gmk70TrialGatt $result 'Characteristic discovery'
    $characters = ConvertTo-Gmk70WinRtArray -Value $result.Characteristics `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic])
    foreach ($char in $characters) {
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff01') { $script:trialCommand = $char }
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff00') { $script:trialData = $char }
    }
    if ($null -eq $script:trialCommand -or $null -eq $script:trialData) { throw 'Missing OTA characteristics.' }
}

function Close-Gmk70TrialRadio {
    $script:trialWriteRanges = @()
    if ($null -ne $script:trialService) { try { $script:trialService.Dispose() } catch { } }
    if ($null -ne $script:trialDevice) { try { $script:trialDevice.Dispose() } catch { } }
}

function Send-Gmk70TrialPacket([byte[]] $Packet) {
    # This check runs at the last boundary before any GATT write.
    Assert-Gmk70TrialPacket -Packet $Packet -WriteRanges $script:trialWriteRanges
    $writer = [Windows.Storage.Streams.DataWriter]::new()
    try {
        $writer.WriteBytes($Packet)
        $buffer = $writer.DetachBuffer()
        $method = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic].GetMethod(
            'WriteValueWithResultAsync', [type[]] @([Windows.Storage.Streams.IBuffer],
                [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]))
        $arguments = [object[]] @($buffer,
            [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]::WriteWithResponse)
        $script:trialSent.Add(@{ opcode = [int] $Packet[0]; address = [BitConverter]::ToUInt32($Packet, 3);
            length = [int] [BitConverter]::ToUInt16($Packet, 7) })
        $result = Wait-Gmk70TrialOperation { $method.Invoke($script:trialCommand, $arguments) } `
            ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteResult]) 'bounded trial request'
        Assert-Gmk70TrialGatt $result 'Trial request'
    }
    finally { $writer.Dispose() }
}

function Read-Gmk70TrialBlock {
    param([uint32] $Address, [int] $Length, [switch] $Storage)
    $packet = if ($Storage) {
        New-Gmk70ReadPacket -Offset $Address -Length $Length -RegionLength 0x80000
    } else { New-Gmk70RamReadPacket -Address $Address -Length $Length }
    Send-Gmk70TrialPacket $packet
    $result = Wait-Gmk70TrialOperation {
        $script:trialData.ReadValueAsync([Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached)
    } ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult]) 'consume trial response'
    Assert-Gmk70TrialGatt $result 'Trial read'
    [byte[]] $bytes = ConvertFrom-Gmk70WinRtBuffer -Buffer $result.Value
    if ($bytes.Length -ne $Length) { throw "Expected $Length bytes; received $($bytes.Length)." }
    return ,$bytes
}

function Read-Gmk70TrialRam {
    param([uint32] $Address, [int] $Length)
    Assert-Gmk70RamRange $Address $Length
    $bytes = [byte[]]::new($Length)
    for ($offset = 0; $offset -lt $Length; $offset += 128) {
        $count = [Math]::Min(128, $Length - $offset)
        [byte[]] $part = Read-Gmk70TrialBlock -Address ($Address + $offset) -Length ([Math]::Max(16, $count))
        [Array]::Copy($part, 0, $bytes, $offset, $count)
    }
    return ,$bytes
}
