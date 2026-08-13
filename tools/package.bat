@echo off
rem Package an already-built PowerPeek into the release assets.
rem   tools\package.bat [-BuildDir <dir>] [-OutputDir <dir>]
rem
rem A wrapper rather than the script itself: PowerShell's default execution policy on
rem client Windows refuses to run a .ps1 file even one written on that machine, so every
rem caller would otherwise need the -ExecutionPolicy incantation below.
setlocal

rem Started from a PowerShell 7 terminal, this inherits a PSModulePath that points at
rem pwsh's own modules. Windows PowerShell then resolves Microsoft.PowerShell.Utility to
rem the 7.x copy and loses half of it -- Get-FileHash simply stops existing. Emptying the
rem variable makes it rebuild the default path for itself.
set "PSModulePath="

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
exit /b %ERRORLEVEL%
