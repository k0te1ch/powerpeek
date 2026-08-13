@echo off
rem Configure and build PowerPeek.
rem   tools\build.bat [debug|release]   (default: release)
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"

set "ROOT=%~dp0.."
call "%~dp0vsenv.bat" || exit /b 1

pushd "%ROOT%" || exit /b 1
"%PP_CMAKE%" --preset %CONFIG% -DCMAKE_MAKE_PROGRAM="%PP_NINJA%" || (popd & exit /b 1)
"%PP_CMAKE%" --build --preset %CONFIG% || (popd & exit /b 1)
popd

echo.
echo [build] %CONFIG% build finished: %ROOT%\build\%CONFIG%\PowerPeek.exe
endlocal
