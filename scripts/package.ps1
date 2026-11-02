param([string]$Version = '0.3.5')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+([-.][A-Za-z0-9]+)*$') { throw 'Invalid version' }
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if (!(Test-Path -LiteralPath dist/codex_meter.exe)) { throw 'Run scripts/build.ps1 first' }
    $binaryVersion = (Get-Item -LiteralPath dist/codex_meter.exe).VersionInfo.ProductVersion
    if ($binaryVersion -ne $Version) { throw "Binary version $binaryVersion does not match package version $Version" }
    $stage = Join-Path 'build' ("package-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path "$stage/third_party/nlohmann" | Out-Null
    Copy-Item -LiteralPath dist/codex_meter.exe,README.md,LICENSE,PRIVACY.md,TESTING.md,CHANGELOG.md,RELEASE_NOTES.md -Destination $stage
    Copy-Item -LiteralPath third_party/nlohmann/LICENSE.MIT -Destination "$stage/third_party/nlohmann"
    $archive = "dist/CodexMeter-$Version-windows-x64.zip"
    Compress-Archive -Path "$stage/*" -DestinationPath $archive -Force
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $(Split-Path -Leaf $archive)" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    Get-Item -LiteralPath $archive,"$archive.sha256" | Select-Object FullName,Length
} finally { Pop-Location }
