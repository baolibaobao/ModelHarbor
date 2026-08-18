[CmdletBinding()]
param(
    [ValidateSet('configure', 'build', 'test', 'all')]
    [string]$Action = 'all',
    [ValidateSet('windows-msvc-debug', 'windows-msvc-release')]
    [string]$Preset = 'windows-msvc-debug',
    [string]$QtRoot = $env:QT_ROOT,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw 'Set QT_ROOT to the Qt 6.11.1 MSVC installation directory.'
}

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw 'Set VCPKG_ROOT to the vcpkg checkout directory.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found: $vswhere"
}

$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw 'A Visual Studio installation with the MSVC x64 workload was not found.'
}

$vsDevCmd = Join-Path $vsInstall 'Common7\Tools\VsDevCmd.bat'
$environmentLines = & cmd.exe /d /s /c "call `"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$env:QT_ROOT = (Resolve-Path -LiteralPath $QtRoot).Path
$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Command $($Arguments -join ' ')"
    }
}

if ($Action -in @('configure', 'all')) {
    Invoke-Checked 'cmake' @('--fresh', '--preset', $Preset)
}

if ($Action -in @('build', 'all')) {
    Invoke-Checked 'cmake' @('--build', '--preset', $Preset)
}

if ($Action -in @('test', 'all')) {
    Invoke-Checked 'ctest' @('--preset', $Preset, '--output-on-failure')
}
