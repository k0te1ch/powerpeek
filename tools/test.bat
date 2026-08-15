@echo off
rem Configure, build and run the unit tests.
rem   tools\test.bat [extra ctest arguments]
rem
rem Example: tools\test.bat -R eventDetector   runs only the event detector cases.
rem
rem The tests have a preset of their own rather than riding on debug or release, because
rem switching them on is the one thing in this build that reaches the network -- doctest is
rem fetched at configure time. On a separate preset, an ordinary build of the application
rem never pays for that and still works with no connection at all.
setlocal

set "ROOT=%~dp0.."
call "%~dp0vsenv.bat" || exit /b 1

pushd "%ROOT%" || exit /b 1
"%PP_CMAKE%" --preset tests -DCMAKE_MAKE_PROGRAM="%PP_NINJA%" || (popd & exit /b 1)
"%PP_CMAKE%" --build --preset tests || (popd & exit /b 1)
"%PP_CTEST%" --preset tests %* || (popd & exit /b 1)
popd

echo.
echo [test] all unit tests passed.
endlocal
