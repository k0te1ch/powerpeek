@echo off
rem Configure, build and run the unit tests under AddressSanitizer.
rem   tools\asan.bat [extra ctest arguments]
rem
rem A separate preset and build tree from tools\test.bat: every object is compiled with
rem /fsanitize=address, which is slower and must never leak into the shipped executable.
setlocal

set "ROOT=%~dp0.."
call "%~dp0vsenv.bat" || exit /b 1

rem The ASan runtime DLL ships next to cl.exe but is not on PATH by default.
for %%c in (cl.exe) do set "PATH=%%~dp$PATH:c;%PATH%"

pushd "%ROOT%" || exit /b 1
"%PP_CMAKE%" --preset asan -DCMAKE_MAKE_PROGRAM="%PP_NINJA%" || (popd & exit /b 1)
"%PP_CMAKE%" --build --preset asan || (popd & exit /b 1)
"%PP_CTEST%" --preset asan %* || (popd & exit /b 1)
popd

echo.
echo [asan] all unit tests passed under AddressSanitizer.
endlocal