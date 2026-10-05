@echo off
setlocal

set "QGC_ROOT=%~dp0"
set "QGC_LAUNCHER=%QGC_ROOT%tools\debug\start-windows-debug.ps1"
set "QGC_EXE=%QGC_ROOT%build-v5.1.5-debug\Debug\QGC_KevinJiang_v5_1_5_Debug.exe"
set "QT_LOGGING_RULES="
set "QGC_LAUNCH_ARGS=%*"
if /I "%~1"=="--diagnostics" set "QGC_LAUNCH_ARGS=-Diagnostics"

cd /d "%QGC_ROOT%"

if not exist "%QGC_LAUNCHER%" (
    echo Debug launcher was not found: "%QGC_LAUNCHER%"
    pause
    exit /b 1
)

if not exist "%QGC_EXE%" (
    echo QGroundControl v5.1.5 Debug was not found: "%QGC_EXE%"
    echo Build the candidate using docs\debug\windows-v5.1.5-debug.md.
    pause
    exit /b 1
)

echo Starting "%QGC_EXE%"
start "QGC_KevinJiang v5.1.5 Debug Logs" /min /D "%QGC_ROOT%" cmd.exe /d /s /c "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%QGC_LAUNCHER%" %QGC_LAUNCH_ARGS% 1>>"%TEMP%\QGC_KevinJiang_v5_1_5_Debug.stdout.log" 2>>"%TEMP%\QGC_KevinJiang_v5_1_5_Debug.stderr.log""
if errorlevel 1 (
    echo Failed to start QGroundControl v5.1.5 Debug.
    pause
    exit /b 1
)

exit /b 0
