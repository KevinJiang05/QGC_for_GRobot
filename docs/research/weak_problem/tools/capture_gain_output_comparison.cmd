@echo off
setlocal EnableExtensions
title QGC Gain Output Comparison
set "PWSH=pwsh.exe"
where pwsh.exe >nul 2>&1
if errorlevel 1 set "PWSH=D:\Develop\envs\tools\PowerShell\7.6.3\pwsh.exe"
"%PWSH%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture_gain_output_comparison.ps1"
set "CAPTURE_EXIT=%ERRORLEVEL%"
echo.
echo The test guide exited with code %CAPTURE_EXIT%.
echo Press any key to close this window.
pause >nul
exit /b %CAPTURE_EXIT%
