@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

call "%SCRIPT_DIR%tools\ai_detection\run_yolo_to_qgc_udp.bat" %*
set "EXIT_CODE=%ERRORLEVEL%"
echo.
if not "%EXIT_CODE%"=="0" (
    echo YOLO bridge exited with code %EXIT_CODE%.
) else (
    echo YOLO bridge stopped.
)
pause
exit /b %EXIT_CODE%
