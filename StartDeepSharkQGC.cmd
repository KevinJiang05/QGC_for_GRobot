@echo off
setlocal

set "GST_ROOT=D:\gstreamer\1.0\msvc_x86_64"
set "QGC_ROOT=D:\Develop\QGC_for_GRobot"

set "PATH=%GST_ROOT%\bin;%PATH%"
set "GST_PLUGIN_PATH=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_PATH_1_0=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH_1_0=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SCANNER=%GST_ROOT%\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GST_PLUGIN_SCANNER_1_0=%GST_ROOT%\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GIO_EXTRA_MODULES=%GST_ROOT%\lib\gio\modules"

cd /d "%QGC_ROOT%"
start "" "%QGC_ROOT%\build\Debug\QGroundControl.exe"
