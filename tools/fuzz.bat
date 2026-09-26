@echo off
rem Build the libFuzzer targets and run each against its seed corpus for a bounded time.
rem   tools\fuzz.bat [seconds per target, default 60]
rem
rem New inputs libFuzzer discovers go to build\fuzz\corpus\<target>, not into the checked-in
rem seeds, so a run never dirties the tree. A crash leaves its reproducer in build\fuzz.
setlocal

set "ROOT=%~dp0.."
set "SECONDS=%~1"
if "%SECONDS%"=="" set "SECONDS=60"
call "%~dp0vsenv.bat" || exit /b 1
for %%c in (cl.exe) do set "PATH=%%~dp$PATH:c;%PATH%"

pushd "%ROOT%" || exit /b 1
"%PP_CMAKE%" --preset fuzz -DCMAKE_MAKE_PROGRAM="%PP_NINJA%" || (popd & exit /b 1)
"%PP_CMAKE%" --build --preset fuzz || (popd & exit /b 1)

for %%t in (wav json) do call :run %%t || (popd & exit /b 1)
popd

echo.
echo [fuzz] no crashes in %SECONDS%s per target.
endlocal
exit /b 0

:run
set "WORK=build\fuzz\corpus\%1"
if not exist "%WORK%" mkdir "%WORK%"
echo [fuzz] pp_fuzz_%1 for %SECONDS%s
build\fuzz\fuzz\pp_fuzz_%1.exe "%WORK%" fuzz\corpus\%1 -max_total_time=%SECONDS% -artifact_prefix=build\fuzz\ -print_final_stats=1
exit /b %errorlevel%