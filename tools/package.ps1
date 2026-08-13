# Packages an already-built PowerPeek into the two things a release ships: the Inno Setup
# installer and the portable zip, plus a SHA-256 sidecar for each.
#   tools\package.ps1 [-BuildDir <dir>] [-OutputDir <dir>]
# Build first: tools\build.bat release

[CmdletBinding()]
param(
    [string] $BuildDir,
    [string] $OutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build\release' }
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }

function Find-InnoSetupCompiler {
    if ($env:PP_ISCC) { return $env:PP_ISCC }

    $onPath = Get-Command ISCC.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    # Inno Setup does not put itself on PATH, and its 64-bit builds still install under the
    # x86 program files by default.
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $base) { continue }
        $candidate = Join-Path $base 'Inno Setup 6\ISCC.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    throw 'ISCC.exe was not found. Install Inno Setup 6.3 or newer from ' +
          'https://jrsoftware.org/isdl.php, or point PP_ISCC at it.'
}

$exe = Join-Path $BuildDir 'PowerPeek.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "There is nothing to package at $exe. Run tools\build.bat release first."
}

$version = (Get-Content (Join-Path $root 'version.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "version.txt must hold a bare MAJOR.MINOR.PATCH version, but holds '$version'."
}

# The asset names come from version.txt while the version in the binary comes from the same
# file through CMake and rc.exe. A stale build would put one version in the file name and a
# different one in the file properties, which is only ever noticed after the release.
$fileVersion = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if ($fileVersion -ne "$version.0") {
    throw "version.txt says $version, but $exe reports $fileVersion. Rebuild it."
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$iscc = Find-InnoSetupCompiler
Write-Host "[package] Inno Setup: $iscc"
& $iscc '/Q' "/O$OutputDir" "/DSourceExe=$exe" (Join-Path $root 'installer\PowerPeek.iss')
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe failed with exit code $LASTEXITCODE."
}

$setup = Join-Path $OutputDir "PowerPeek-Setup-$version.exe"
if (-not (Test-Path -LiteralPath $setup)) {
    throw "ISCC.exe reported success but $setup is not there."
}

# Staged rather than zipped in place: the archive must contain PowerPeek.exe, the readme and
# the licence at its root, with no build-directory clutter and no nested folder.
$stage = Join-Path $OutputDir "portable-$version"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'installer\portable\README.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')

$portable = Join-Path $OutputDir "PowerPeek-$version-portable.zip"
if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $portable -CompressionLevel Optimal
Remove-Item -LiteralPath $stage -Recurse -Force

# sha256sum -c reads this format, so a user can verify a download without a checksum tool.
foreach ($asset in @($setup, $portable)) {
    $hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLower()
    Set-Content -LiteralPath "$asset.sha256" -Value "$hash *$(Split-Path -Leaf $asset)" -Encoding ascii
}

Write-Host ''
Write-Host "[package] PowerPeek $version packaged into $OutputDir"
Get-ChildItem -LiteralPath $OutputDir -File | ForEach-Object {
    Write-Host ('    {0,-40} {1,8:N0} KB' -f $_.Name, ($_.Length / 1KB))
}
