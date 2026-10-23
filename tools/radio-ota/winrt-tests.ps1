# Offline checks only: no Bluetooth discovery or device commands.
#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'radio-core.ps1')

function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) { throw "$Label expected '$Expected', received '$Actual'." }
}

# Collection cardinality must survive PowerShell's pipeline unrolling.
foreach ($count in @(0, 1, 2)) {
    $values = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $count; $i++) { $values.Add('service-' + $i) }
    $items = ConvertTo-Gmk70WinRtArray -Value $values.AsReadOnly() -ItemType ([string])
    Assert-Equal $items.Length $count 'Projected collection length'
    for ($i = 0; $i -lt $count; $i++) {
        Assert-Equal $items[$i] ('service-' + $i) 'Projected collection item'
    }
}
Write-Host 'PASS: empty, single and multiple collection items retain array shape.'

if ($PSVersionTable.PSEdition -eq 'Desktop') {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    [Windows.System.UserProfile.GlobalizationPreferences, Windows.System.UserProfile, ContentType = WindowsRuntime] | Out-Null
    $languages = ConvertTo-Gmk70WinRtArray -Value ([Windows.System.UserProfile.GlobalizationPreferences]::Languages) -ItemType ([string])
    if ($languages.Length -lt 1 -or [string]::IsNullOrEmpty($languages[0])) {
        throw 'A native WinRT vector must expose its items through the array adapter.'
    }
    Write-Host 'PASS: actual native WinRT COM vector supports counting and indexing.'

    [Windows.Security.Cryptography.CryptographicBuffer, Windows.Security.Cryptography, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    $buffer = [Windows.Security.Cryptography.CryptographicBuffer]::DecodeFromHexString('010203ff')
    $bytes = ConvertFrom-Gmk70WinRtBuffer -Buffer $buffer
    Assert-Equal ([BitConverter]::ToString($bytes)) '01-02-03-FF' 'Native buffer bytes'
    $empty = [Windows.Security.Cryptography.CryptographicBuffer]::CreateFromByteArray([byte[]]::new(0))
    Assert-Equal (ConvertFrom-Gmk70WinRtBuffer -Buffer $empty).Length 0 'Empty native buffer'
    Assert-Equal (ConvertFrom-Gmk70WinRtBuffer -Buffer $null).Length 0 'Absent native buffer'
    Write-Host 'PASS: actual native WinRT buffers retain exact bytes and empty array shape.'
}
