@echo off
rem Puts the Visual Studio 2022 x64 toolchain on PATH and exports VSPATH.
rem Meant to be CALLed from the other scripts, not run on its own.

if defined VSPATH goto :haveVs

rem The vswhere lookup writes to a temporary file rather than going through `for /f`
rem over a backticked command: the installer path contains "(x86)", and an unquoted
rem closing parenthesis inside an `in (...)` clause is a long-standing cmd parsing trap.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :probeKnownPaths

set "VSQUERY=%TEMP%\pp-vspath-%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSQUERY%" 2>nul
set /p VSPATH=<"%VSQUERY%"
del "%VSQUERY%" >nul 2>&1
if defined VSPATH goto :haveVs

:probeKnownPaths
for %%e in (Enterprise Professional Community BuildTools) do call :tryEdition "%%e"
if not defined VSPATH goto :notFound
goto :haveVs

:tryEdition
if defined VSPATH exit /b 0
set "CANDIDATE=%ProgramFiles%\Microsoft Visual Studio\2022\%~1"
if exist "%CANDIDATE%\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%CANDIDATE%"
exit /b 0

:notFound
echo [vsenv] Visual Studio 2022 with the "Desktop development with C++" workload was not found.
exit /b 1

:haveVs
if defined PP_VSENV_READY goto :tools

rem vcvars64.bat writes to stderr even on a successful run, so both streams are dropped;
rem a genuine failure is re-run visibly rather than swallowed.
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :vcvarsFailed
set "PP_VSENV_READY=1"

:tools
rem The CMake and Ninja bundled with Visual Studio are newer than most standalone
rem installs and always match the toolset, so prefer them when they are there.
set "PP_CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "PP_CTEST=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
set "PP_NINJA=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%PP_CMAKE%" set "PP_CMAKE=cmake"
if not exist "%PP_CTEST%" set "PP_CTEST=ctest"
if not exist "%PP_NINJA%" set "PP_NINJA=ninja"
exit /b 0

:vcvarsFailed
echo [vsenv] vcvars64.bat failed. Re-running it so the error is visible:
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
exit /b 1
