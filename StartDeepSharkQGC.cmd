@echo off
setlocal

set "GST_ROOT=D:\Develop\Toolchains\GStreamer\1.0\msvc_x86_64"
set "QT_ROOT=D:\Develop\Toolchains\Qt\6.8.3\msvc2022_64"
set "QGC_ROOT=D:\Develop\QGC_for_GRobot"
set "VS_ROOT=D:\Develop\Toolchains\VS2022BuildTools"
set "VS_CRT=%VS_ROOT%\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
set "VS_DEBUG_CRT=%VS_ROOT%\VC\Redist\MSVC\14.44.35112\debug_nonredist\x64\Microsoft.VC143.DebugCRT"
set "WIN_UCRT_DEBUG=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\ucrt"
set "QGC_EXE="

if exist "%QGC_ROOT%\build-debug-ai\Debug\QGC_KevinJiang.exe" (
    set "QGC_EXE=%QGC_ROOT%\build-debug-ai\Debug\QGC_KevinJiang.exe"
) else if exist "%QGC_ROOT%\build-release\Release\QGC_KevinJiang.exe" (
    set "QGC_EXE=%QGC_ROOT%\build-release\Release\QGC_KevinJiang.exe"
) else if exist "%QGC_ROOT%\build-release\package-root\bin\QGC_KevinJiang.exe" (
    set "QGC_EXE=%QGC_ROOT%\build-release\package-root\bin\QGC_KevinJiang.exe"
) else if exist "%QGC_ROOT%\build-release\staging\bin\QGC_KevinJiang.exe" (
    set "QGC_EXE=%QGC_ROOT%\build-release\staging\bin\QGC_KevinJiang.exe"
)

set "PATH=%QT_ROOT%\bin;%GST_ROOT%\bin;%VS_CRT%;%VS_DEBUG_CRT%;%WIN_UCRT_DEBUG%;%PATH%"
set "GST_PLUGIN_PATH=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_PATH_1_0=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH_1_0=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SCANNER=%GST_ROOT%\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GST_PLUGIN_SCANNER_1_0=%GST_ROOT%\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GIO_EXTRA_MODULES=%GST_ROOT%\lib\gio\modules"
set "QT_LOGGING_RULES=qgc.videomanager.videoreceiver.gstreamer*.debug=true;qgc.videomanager.videoreceiver.gstreamer*.warning=true;qgc.videomanager.videoreceiver.gstreamer*.critical=true"

cd /d "%QGC_ROOT%"

if not defined QGC_EXE (
    echo QGC_KevinJiang.exe was not found.
    echo Checked normal build-debug-ai and build-release outputs.
    pause
    exit /b 1
)

echo Starting "%QGC_EXE%"
start "QGC_KevinJiang Logs" /min /D "%QGC_ROOT%" cmd.exe /d /s /c ""%QGC_EXE%" 1>>"%TEMP%\QGC_KevinJiang.stdout.log" 2>>"%TEMP%\QGC_KevinJiang.stderr.log""
if errorlevel 1 (
    echo Failed to start QGC_KevinJiang.exe.
    pause
    exit /b 1
)

exit /b 0
