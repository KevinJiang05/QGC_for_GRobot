@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "MEDIAMTX_DIR=%SCRIPT_DIR%mediamtx_v1.19.2_windows_amd64"
set "MEDIAMTX_EXE=%MEDIAMTX_DIR%\mediamtx.exe"
set "MEDIAMTX_CONFIG=%SCRIPT_DIR%mediamtx-deepshark.yml"
set "MEDIAMTX_LOG=%TEMP%\DeepShark_MediaMTX.log"

if not exist "%MEDIAMTX_EXE%" (
    echo mediamtx.exe was not found: "%MEDIAMTX_EXE%"
    exit /b 1
)

if not exist "%MEDIAMTX_CONFIG%" (
    echo MediaMTX config was not found: "%MEDIAMTX_CONFIG%"
    exit /b 1
)

netstat -ano | findstr /R /C:":8554 .*LISTENING" >nul
if not errorlevel 1 (
    echo DeepShark RTSP server is already listening on 127.0.0.1:8554.
    exit /b 0
)

echo Starting DeepShark RTSP server on rtsp://127.0.0.1:8554/deepshark
start "DeepShark RTSP Server" /min /D "%MEDIAMTX_DIR%" cmd.exe /d /s /c ""%MEDIAMTX_EXE%" "%MEDIAMTX_CONFIG%" 1>>"%MEDIAMTX_LOG%" 2>>&1"

for /l %%I in (1,1,20) do (
    ping -n 2 127.0.0.1 >nul
    netstat -ano | findstr /R /C:":8554 .*LISTENING" >nul
    if not errorlevel 1 (
        echo DeepShark RTSP server is ready.
        exit /b 0
    )
)

echo DeepShark RTSP server did not start within 20 seconds. See "%MEDIAMTX_LOG%".
exit /b 1
