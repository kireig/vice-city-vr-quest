@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Vice City VR - Update
echo.
echo VICE CITY VR - UPDATE FROM GIT AND INSTALL
echo Reuses downloaded tools. Updates only the APK, preserving game data.
echo Close the game on your Quest before updating.
echo.
if not exist "%~dp0tools\update-and-install.ps1" (
  echo ERROR: Keep UPDATE.bat inside the complete Vice City VR source kit.
  set "VCVR_UPDATE_EXIT=1"
  goto finished
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\update-and-install.ps1" %*
set "VCVR_UPDATE_EXIT=%ERRORLEVEL%"
:finished
echo.
if not "%VCVR_UPDATE_EXIT%"=="0" echo UPDATE FAILED. Read the error above; the game was not launched.
echo Press any key to close this window.
pause >nul
exit /b %VCVR_UPDATE_EXIT%
