[CmdletBinding()]
param(
    [string]$Preset = 'windows-msvc-release',
    [string]$QtRoot = $env:QT_ROOT,
    [string]$OutputDirectory = 'dist/modelharbor-stage1'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $root "build/$Preset"
$binaryDirectory = Join-Path $buildDirectory 'bin'
$desktop = Join-Path $binaryDirectory 'modelharbor-desktop.exe'
$gateway = Join-Path $binaryDirectory 'modelharbor-gateway.exe'
if (-not (Test-Path -LiteralPath $desktop) -or -not (Test-Path -LiteralPath $gateway)) {
    throw "Build the $Preset preset before packaging."
}
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw 'Set QT_ROOT or pass -QtRoot.'
}

$output = [System.IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$rootPrefix = [System.IO.Path]::GetFullPath($root) + [System.IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must stay inside the repository.'
}
if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null

Copy-Item -LiteralPath $desktop, $gateway -Destination $output
Get-ChildItem -LiteralPath $binaryDirectory -File -Filter *.dll | Copy-Item -Destination $output
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.md') -Destination $output

$windeployqt = Join-Path $QtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt.exe was not found: $windeployqt"
}
& $windeployqt --release --no-translations --compiler-runtime (Join-Path $output 'modelharbor-desktop.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

& (Join-Path $PSScriptRoot 'New-ThirdPartyNotices.ps1') `
    -BuildDirectory "build/$Preset" `
    -OutputPath "$OutputDirectory/THIRD_PARTY_NOTICES.md" | Out-Null

Write-Output $output
