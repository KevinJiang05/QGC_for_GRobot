@echo off
setlocal EnableExtensions
title QGC MAVLink Capture
set "PWSH=pwsh.exe"
where pwsh.exe >nul 2>&1
if errorlevel 1 set "PWSH=D:\Develop\envs\tools\PowerShell\7.6.3\pwsh.exe"
if not exist "%PWSH%" if "%PWSH%"=="D:\Develop\envs\tools\PowerShell\7.6.3\pwsh.exe" (
    echo PowerShell 7 was not found.
    echo Press any key to close this window.
    pause >nul
    exit /b 10
)
"%PWSH%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture_qgc_manual_control.ps1"
set "CAPTURE_EXIT=%ERRORLEVEL%"
echo.
echo The capture program exited with code %CAPTURE_EXIT%.
echo Press any key to close this window.
pause >nul
exit /b %CAPTURE_EXIT%
