# Renders the winget manifest templates in packaging\winget into a version, installer and
# locale manifest set for one release, substituting __VERSION__, __INSTALLER_URL__ and
# __INSTALLER_SHA256__.
#   tools\render-winget-manifest.ps1 -Version <ver> -InstallerUrl <url> -InstallerSha256 <hash> [-OutputDir <dir>]
#
# This only renders the files; it does not validate or submit them. See
# packaging\winget\README.md for what happens to the result.

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $InstallerUrl,
    [Parameter(Mandatory)] [string] $InstallerSha256,
    [string] $OutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must be a bare MAJOR.MINOR.PATCH, but got '$Version'."
}
if ($InstallerSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "InstallerSha256 must be a 64-character hex string, but got '$InstallerSha256'."
}

$root = Split-Path -Parent $PSScriptRoot
$templateDir = Join-Path $root 'packaging\winget'
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist\winget' }

if (Test-Path -LiteralPath $OutputDir) { Remove-Item -LiteralPath $OutputDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# winget's own tooling expects the hash upper-cased; tools\package.ps1 writes the
# lower-case sidecar sha256sum -c reads, so the two conventions have to meet somewhere.
$upperSha256 = $InstallerSha256.ToUpperInvariant()

$templates = Get-ChildItem -LiteralPath $templateDir -Filter '*.yaml'
foreach ($template in $templates) {
    $content = Get-Content -LiteralPath $template.FullName -Raw
    $content = $content.Replace('__VERSION__', $Version)
    $content = $content.Replace('__INSTALLER_URL__', $InstallerUrl)
    $content = $content.Replace('__INSTALLER_SHA256__', $upperSha256)
    Set-Content -LiteralPath (Join-Path $OutputDir $template.Name) -Value $content -NoNewline
}

Write-Host "[render-winget-manifest] rendered $($templates.Count) files into $OutputDir"
