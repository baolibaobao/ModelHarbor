[CmdletBinding()]
param(
    [ValidateSet('format', 'format-check', 'tidy')]
    [string]$Action = 'format-check',
    [string]$ClangFormat,
    [string]$ClangTidy,
    [string]$BuildDirectory = 'build/windows-msvc-debug'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') `
    -Recurse -File | Where-Object { $_.Extension -in @('.cpp', '.h') } |
    ForEach-Object FullName

function Resolve-Tool {
    param([string]$ExplicitPath, [string]$CommandName, [string[]]$Candidates)
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "$CommandName was not found. Pass its path explicitly."
}

if ($Action -in @('format', 'format-check')) {
    $formatter = Resolve-Tool $ClangFormat 'clang-format' @(
        'F:\Qt\Tools\QtCreator\bin\clang\bin\clang-format.exe',
        'F:\Qt\Tools\llvm-mingw1706_64\bin\clang-format.exe'
    )
    foreach ($sourceFile in $sourceFiles) {
        if ($Action -eq 'format') {
            & $formatter '-i' $sourceFile
        } else {
            & $formatter '--dry-run' '--Werror' $sourceFile
        }
        if ($LASTEXITCODE -ne 0) {
            throw "clang-format failed for $sourceFile with exit code $LASTEXITCODE"
        }
    }
    return
}

$tidy = Resolve-Tool $ClangTidy 'clang-tidy' @(
    'F:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe'
)
$compileCommands = Join-Path $root $BuildDirectory
if (-not (Test-Path -LiteralPath (Join-Path $compileCommands 'compile_commands.json'))) {
    throw "compile_commands.json was not found in $compileCommands"
}
foreach ($sourceFile in $sourceFiles) {
    if ([System.IO.Path]::GetExtension($sourceFile) -ne '.cpp') { continue }
    & $tidy '-p' $compileCommands $sourceFile
    if ($LASTEXITCODE -ne 0) {
        throw "clang-tidy failed for $sourceFile with exit code $LASTEXITCODE"
    }
}
