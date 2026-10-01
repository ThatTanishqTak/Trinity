@echo off

setlocal
pushd "%~dp0.."

powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts\Bootstrap.ps1" || goto :failed
cmake --preset windows-vs2026 || goto :failed

echo.
echo Solution generated in Build\windows-vs2026 - open it in Visual Studio 2026.
popd
PAUSE
exit /b 0

:failed
echo.
echo Setup failed. See the messages above.
popd
PAUSE
exit /b 1