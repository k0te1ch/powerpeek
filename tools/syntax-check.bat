@echo off
rem Compile one translation unit in isolation, with the project's real flags.
rem   tools\syntax-check.bat src\core\Json.cpp [more.cpp ...]
rem
rem Useful while a module is being written and the rest of the project does not link yet:
rem this only compiles, so definitions missing elsewhere do not matter.
setlocal

if "%~1"=="" (
    echo usage: tools\syntax-check.bat ^<source.cpp^> [more.cpp ...]
    exit /b 2
)

set "ROOT=%~dp0.."

rem version.txt is the one place the version lives, exactly as it is for CMake: a literal
rem here would keep compiling these translation units against 1.0.0 long after a release
rem moved the file on, so anything guarded on PP_VERSION_* would be checked against a
rem version the real build never sees.
set "PP_VERSION="
for /f "usebackq delims=" %%v in ("%ROOT%\version.txt") do if not defined PP_VERSION set "PP_VERSION=%%v"
echo(%PP_VERSION%| findstr /r /c:"^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul || goto :badVersion
for /f "tokens=1-3 delims=." %%a in ("%PP_VERSION%") do (
    set "PP_VERSION_MAJOR=%%a"
    set "PP_VERSION_MINOR=%%b"
    set "PP_VERSION_PATCH=%%c"
)

call "%~dp0vsenv.bat" || exit /b 1

set "OUTDIR=%ROOT%\build\syntax-check"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /c /std:c++20 /permissive- /W4 /utf-8 /EHsc /bigobj /Zc:__cplusplus ^
   /external:anglebrackets /external:W0 /wd4100 ^
   /DUNICODE /D_UNICODE /DSTRICT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOSERVICE /DNOMCX /DNOIME ^
   /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DNTDDI_VERSION=0x0A000000 ^
   /DPP_VERSION_STRING=\"%PP_VERSION%\" /DPP_VERSION_MAJOR=%PP_VERSION_MAJOR% ^
   /DPP_VERSION_MINOR=%PP_VERSION_MINOR% /DPP_VERSION_PATCH=%PP_VERSION_PATCH% ^
   /I "%ROOT%\src" /I "%ROOT%\resources" ^
   /Fo"%OUTDIR%\\" %*
if errorlevel 1 (
    echo.
    echo [syntax-check] FAILED
    exit /b 1
)

echo [syntax-check] OK
endlocal
exit /b 0

:badVersion
echo [syntax-check] version.txt must hold a bare MAJOR.MINOR.PATCH version, but holds "%PP_VERSION%".
exit /b 1
