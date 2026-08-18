[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/windows-msvc-release',
    [string]$OutputPath = 'dist/modelharbor-stage1/THIRD_PARTY_NOTICES.md'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$output = [System.IO.Path]::GetFullPath((Join-Path $root $OutputPath))
$share = Join-Path $build 'vcpkg_installed/x64-windows/share'
if (-not (Test-Path -LiteralPath $share)) {
    throw "vcpkg share directory was not found: $share"
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# ModelHarbor Third-Party Notices')
$lines.Add('')
$lines.Add('Generated from the selected build directory by New-ThirdPartyNotices.ps1.')
$lines.Add('Qt deployment and final license text are fixed during the release-candidate stage; this stage records vcpkg packages and repository icon resources.')

$copyrightFiles = Get-ChildItem -LiteralPath $share -Recurse -File -Filter copyright |
    Sort-Object FullName
foreach ($file in $copyrightFiles) {
    $package = Split-Path -Leaf $file.DirectoryName
    $lines.Add('')
    $lines.Add("## $package")
    $lines.Add('')
    $lines.Add('```text')
    $lines.AddRange([string[]](Get-Content -LiteralPath $file.FullName))
    $lines.Add('```')
}

$lucideLicense = Join-Path $root 'resources/lucide/LICENSE'
$lines.Add('')
$lines.Add('## lucide-icons')
$lines.Add('')
$lines.Add('```text')
$lines.AddRange([string[]](Get-Content -LiteralPath $lucideLicense))
$lines.Add('```')

$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$lines | Set-Content -LiteralPath $output -Encoding utf8
Write-Output $output
