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
call "%~dp0vsenv.bat" || exit /b 1

set "OUTDIR=%ROOT%\build\syntax-check"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /c /std:c++20 /permissive- /W4 /utf-8 /EHsc /bigobj /Zc:__cplusplus ^
   /external:anglebrackets /external:W0 /wd4100 ^
   /DUNICODE /D_UNICODE /DSTRICT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOSERVICE /DNOMCX /DNOIME ^
   /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DNTDDI_VERSION=0x0A000000 ^
   /DPP_VERSION_STRING=\"1.0.0\" /DPP_VERSION_MAJOR=1 /DPP_VERSION_MINOR=0 /DPP_VERSION_PATCH=0 ^
   /I "%ROOT%\src" /I "%ROOT%\resources" ^
   /Fo"%OUTDIR%\\" %*
if errorlevel 1 (
    echo.
    echo [syntax-check] FAILED
    exit /b 1
)

echo [syntax-check] OK
endlocal
