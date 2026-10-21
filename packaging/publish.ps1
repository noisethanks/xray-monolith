# Packages existing build outputs. publish.bat performs the clean rebuild first.
# Pack only: powershell -NoProfile -File packaging\publish.ps1 [-Version ...] [-OutputDirectory ...]
[CmdletBinding()]
param(
    [string]$Version,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildOutput = Join-Path $repoRoot '_build\_game\bin_dbg'
$executables = @('AnomalyDX10.exe', 'AnomalyDX10AVX.exe', 'AnomalyDX11.exe', 'AnomalyDX11AVX.exe')
$shaderFiles = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'shader-files.txt') |
    ForEach-Object { $_.Trim().Replace('\', '/') } | Where-Object { $_ -and !$_.StartsWith('#') })

if (!$Version) { $Version = $env:PUBLISH_VERSION }
if (!$Version) {
    $revision = & git -C $repoRoot rev-parse --short=8 HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the source revision.' }
    $Version = '{0}-{1}' -f (Get-Date -Format 'yyyy.MM.dd'), $revision
}
if ($Version -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
    throw 'Version must contain only letters, numbers, dots, underscores, and hyphens.'
}
if (!$OutputDirectory) { $OutputDirectory = $env:PUBLISH_DIR }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot '_build\publish' }
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$packagePath = Join-Path $outputRoot "Sam-Optimization-Patches-MT_$Version.zip"

# Use tracked paths, but their current working-tree contents. Do not ship local
# scratch files or the upstream scripts/.vscode editor settings.
$gamedataFiles = @(& git -C $repoRoot -c core.quotepath=false ls-files -- gamedata)
if ($LASTEXITCODE -ne 0 -or !$gamedataFiles.Count) { throw 'Cannot enumerate gamedata.' }
$gamedataFiles = @($gamedataFiles | Where-Object { $_ -notmatch '(^|/)\.' })
foreach ($relative in @('packaging/INSTALL.txt', 'packaging/GRASS-ONLY.txt', 'License.txt', 'compressor/xrCompress.exe',
        'compressor/xrCore.dll', 'compressor/build_patch.ltx') + $gamedataFiles + $shaderFiles) {
    if (![IO.File]::Exists((Join-Path $repoRoot $relative))) { throw "Required file missing: $relative" }
}
foreach ($name in $executables) {
    if (![IO.File]::Exists((Join-Path $buildOutput $name))) { throw "Build output missing: $name. Run build-mt.bat first." }
}
foreach ($relative in $shaderFiles) {
    if ($gamedataFiles -notcontains $relative) { throw "Shader manifest entry is not tracked gamedata: $relative" }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$stage = [IO.Path]::GetFullPath((Join-Path $outputRoot ('stage-' + [Guid]::NewGuid().ToString('N'))))
$release = Join-Path $stage 'release'
$compressor = Join-Path $stage 'compressor'
$overlay = Join-Path $stage 'sss-overlay'

function Copy-PackageFile([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

try {
    foreach ($name in $executables) {
        Copy-PackageFile (Join-Path $buildOutput $name) (Join-Path $release "bin\$name")
    }
    Copy-PackageFile (Join-Path $PSScriptRoot 'INSTALL.txt') (Join-Path $release 'INSTALL.txt')
    Copy-PackageFile (Join-Path $PSScriptRoot 'GRASS-ONLY.txt') (Join-Path $release 'GRASS-ONLY.txt')
    Copy-PackageFile (Join-Path $repoRoot 'License.txt') (Join-Path $release 'License.txt')

    foreach ($relative in $gamedataFiles) {
        Copy-PackageFile (Join-Path $repoRoot $relative) (Join-Path $compressor $relative)
    }
    foreach ($relative in $shaderFiles) {
        # Loose copies also override an older engine's shaders when upgrading.
        Copy-PackageFile (Join-Path $compressor $relative) (Join-Path $release $relative)
    }

    foreach ($name in @('xrCompress.exe', 'xrCore.dll', 'build_patch.ltx')) {
        Copy-PackageFile (Join-Path $repoRoot "compressor\$name") (Join-Path $compressor $name)
    }
    Write-Host 'Packing matching engine support data...'
    $packProcess = Start-Process -FilePath (Join-Path $compressor 'xrCompress.exe') `
        -ArgumentList @('gamedata', '-ltx', 'build_patch.ltx', '-pack', '-strong', '-1024', '-db') `
        -WorkingDirectory $compressor -WindowStyle Hidden -Wait -PassThru
    if ($packProcess.ExitCode -ne 0) { throw "xrCompress failed with exit code $($packProcess.ExitCode)." }
    $firstArchive = Join-Path $compressor 'gamedata.db0'
    if (!(Test-Path -LiteralPath $firstArchive) -or (Get-Item -LiteralPath $firstArchive).Length -eq 0) {
        throw 'xrCompress did not produce gamedata.db0.'
    }
    foreach ($archive in Get-ChildItem -LiteralPath $compressor -File) {
        if ($archive.Name -match '^gamedata\.db([0-9]+)$') {
            # Preserve upstream's name so extraction replaces its old data.
            Copy-PackageFile $archive.FullName (Join-Path $release "db\mods\00_modded_exes_gamedata.db$($Matches[1])")
        }
    }

    foreach ($relative in $shaderFiles) {
        Copy-PackageFile (Join-Path $repoRoot $relative) (Join-Path $overlay $relative)
    }
    $modManager = Join-Path $release 'mod-manager'
    New-Item -ItemType Directory -Path $modManager -Force | Out-Null
    Compress-Archive -LiteralPath (Join-Path $overlay 'gamedata') `
        -DestinationPath (Join-Path $modManager 'Sam-Optimization-Patches-Shaders.zip') -CompressionLevel Optimal

    # Write a complete new archive before replacing an existing publication.
    $stagedZip = Join-Path $stage 'package.zip'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($release, $stagedZip, 'Optimal', $false)
    Move-Item -LiteralPath $stagedZip -Destination $packagePath -Force
    Write-Host "Published: $packagePath"
}
finally {
    # Only delete the unique staging directory created beneath this output root.
    $outputPrefix = $outputRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (!$stage.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $stage) -notmatch '^stage-[0-9a-f]{32}$') {
        throw "Refusing to clean staging directory outside the publish output: $stage"
    }
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
