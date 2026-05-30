@echo off
setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "REPO_DIR=%SCRIPT_DIR%..\.."
set "YOLO_PYTHON=D:\Develop\envs\yolo\Scripts\python.exe"
set "DEFAULT_MODEL=D:\Develop\envs\yolo\models\yolov8n.pt"

if not exist "%YOLO_PYTHON%" (
    echo Shared YOLO Python not found:
    echo   %YOLO_PYTHON%
    echo.
    echo Create it with:
    echo   python -m venv D:\Develop\envs\yolo
    echo   D:\Develop\envs\yolo\Scripts\python.exe -m pip install -r tools\ai_detection\requirements.txt
    exit /b 1
)

set "FIRST_ARG=%~1"

if "%~1"=="" (
    pushd "%REPO_DIR%" >nul
    "%YOLO_PYTHON%" tools\ai_detection\run_yolo_to_qgc_auto.py --model "%DEFAULT_MODEL%"
    set "EXIT_CODE=%ERRORLEVEL%"
    popd >nul
    exit /b %EXIT_CODE%
) else if "!FIRST_ARG:~0,2!"=="--" (
    pushd "%REPO_DIR%" >nul
    "%YOLO_PYTHON%" tools\ai_detection\run_yolo_to_qgc_auto.py --model "%DEFAULT_MODEL%" %*
    set "EXIT_CODE=%ERRORLEVEL%"
    popd >nul
    exit /b %EXIT_CODE%
) else (
    set "SOURCE=%~1"
    shift
)

pushd "%REPO_DIR%" >nul
"%YOLO_PYTHON%" tools\ai_detection\yolo_to_qgc_udp.py --source "%SOURCE%" --model "%DEFAULT_MODEL%" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd >nul

exit /b %EXIT_CODE%
