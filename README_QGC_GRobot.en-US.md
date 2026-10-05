# QGC_GRobot / DeepShark Ground Station

This repository is a **QGroundControl v5.0.8 Stable** custom ground station for underwater robotics competition work and later AUV/ROV experiments. The current internal Windows release is **QGC_KevinJiang v1.3.1**. The project keeps the native QGC ArduPilot, MAVLink, parameter, mode, telemetry, and control stack intact, then layers a DeepShark video panel, a YOLO object-detection overlay, and device-configuration tools through the QGC custom build mechanism.

The guiding rule is minimal, scoped change: reuse QGC's existing video, GStreamer, MAVLink, and settings infrastructure wherever possible, and avoid unrelated architecture rewrites.

## Current Features

- DeepShark custom build is enabled and loads `DeepSharkPlugin`.
- Fly View loads the DeepShark four-channel video panel.
- Four RTSP video sources with saved channel names and URLs.
- Layouts: 2x2 grid, main + auxiliary, in-panel fullscreen, minimized panel, and map visibility toggle.
- Independent Start, Stop, and Reconnect per video channel, with separate state, retry count, and watchdog.
- Sequential Reconnect All to avoid all four streams competing for resources at once.
- YOLO detection boxes can be drawn over both native QGC video and the DeepShark four-channel Video Panel.
- Detection routing by source: `deepSharkVideo1` through `deepSharkVideo4` map to the four video tiles.
- `YOLO Detection Overlay` can be toggled from both QGC Video Settings and the DeepShark settings panel.
- The YOLO bridge can automatically read the four saved RTSP URLs from QGC settings and launch one detector process per source.
- ArduSub 4.8 parameter metadata and firmware-backed joystick actions are available, including `actuator_4_inc/dec`.
- A Servo12 gripper can use Actuator4 for hold-to-move and release-to-hold operation.
- Thruster testing, direct physical-output PWM, and servo jog are separate test paths.
- Video decoder priority is visible so operators can select default, software, or hardware decoding.

## Layout

DeepShark custom code is kept under `custom/` as much as possible. AI bridge tools live under `tools/ai_detection/`.

```text
custom/
  CMakeLists.txt
  custom.qrc
  cmake/
    CustomOverrides.cmake
  src/
    DeepSharkPlugin.h/.cc
    DeepSharkVideoController.h/.cc
    DeepSharkVideoSettings.h/.cc
    AIDetectionManager.h/.cc
    AIDetectionReceiver.h/.cc
    FlyViewCustomLayer.qml
    DeepShark/
      FourVideoPanel.qml
      VideoTile.qml
      DeepSharkStatusPanel.qml
      ThrusterMappingTool.qml
      AIDetectionVideoOverlay.qml
      AIDetectionSettings.qml

tools/ai_detection/
  yolo_to_qgc_udp.py
  run_yolo_to_qgc_auto.py
  run_yolo_to_qgc_udp.bat
  send_sample_detection.py
  requirements.txt

tools/diagnostics/
  check-grobot-link.ps1

tools/release/
  build-windows-release.ps1
```

## Startup Scripts

The repository root provides three development/field helper scripts:

```powershell
.\StartDeepSharkQGC.cmd
.\StartYoloToQGC.cmd
.\StopDeepSharkQGC.cmd
```

`StartDeepSharkQGC.cmd` is a **local development** entry point. It sets the Qt, GStreamer, and MSVC runtime environment variables, then prefers to start:

```text
build-debug-ai\Debug\QGC_KevinJiang.exe
```

Do not launch the Debug video build by double-clicking the exe directly. RTSP/GStreamer depends on the environment prepared by the launcher. The installed release carries its runtime dependencies and should be started from the Windows Start menu or installation directory; it must not depend on this machine's `D:\Develop` toolchain.

`StartYoloToQGC.cmd` starts the YOLO bridge using the shared YOLO environment. When run without arguments, it automatically reads the DeepShark four-channel RTSP URLs saved by QGC.

## Windows Development Environment

Current local paths:

```text
Repository:      D:\Develop\QGC_for_GRobot
Build dir:       D:\Develop\QGC_for_GRobot\build-debug-ai
Qt:              D:\Develop\Toolchains\Qt\6.8.3\msvc2022_64
GStreamer:       D:\Develop\Toolchains\GStreamer\1.0\msvc_x86_64
VS Build Tools:  D:\Develop\Toolchains\VS2022BuildTools
YOLO shared env: D:\Develop\envs\yolo
```

On another machine, paths may differ, but the expected stack is:

- Qt 6.8.x MSVC 64-bit
- Visual Studio 2022 C++ Build Tools
- CMake
- Ninja
- GStreamer MSVC x86_64 runtime/development package
- Python venv with `ultralytics`

## Build

Run from the repository root:

```powershell
cmd /s /c ""D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build build-debug-ai --target QGC_KevinJiang"
```

If the linker reports:

```text
LINK : fatal error LNK1168: cannot open Debug\QGC_KevinJiang.exe for writing
```

QGC is still running and the exe is locked. Close QGC first, or run:

```powershell
Get-Process QGC_KevinJiang -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "*QGC_for_GRobot*build-debug-ai*" } |
    Stop-Process
```

Then build again.

## Windows Release

For a Windows installer, use the standardized release script:

```powershell
.\tools\release\build-windows-release.ps1 -Version 1.3.2
```

The script writes step logs, `release-state.json`, and `release-report.md`. Distribute only after the final report succeeds and version, dependencies, hash, and in-place-upgrade checks pass. See `docs/releases/windows-release-runbook.md`; the current v1.3.1 notes are in `docs/releases/v1.3.1.md`.

## RTSP Workflow

1. Start RTSP streaming from the phone or camera.
2. Verify the URL with VLC or PotPlayer first.
3. Run `StartDeepSharkQGC.cmd`.
4. Open Fly View and show the DeepShark Video Panel.
5. Click `Settings` and enter the four video names and RTSP URLs.
6. Click `Start` or `Reconnect All`.

Common RTSP URL examples:

```text
rtsp://192.168.2.189:8552/live
rtsp://user:password@192.168.2.189:8554/stream1
```

Internal channel IDs:

```text
video1       -> deepSharkVideo1
video2       -> deepSharkVideo2
down view    -> deepSharkVideo3
spare/side   -> deepSharkVideo4
```

## YOLO Detection Overlay

QGC listens on UDP `127.0.0.1:57610`, receives JSON detection results, and draws boxes. YOLO inference runs in an external Python bridge process; QGC itself does not load PyTorch.

Recommended startup order:

1. Run `StartDeepSharkQGC.cmd`.
2. Save the four RTSP URLs in DeepShark settings.
3. Confirm the target video stream is playing.
4. Run `StartYoloToQGC.cmd`.

When `StartYoloToQGC.cmd` is run without arguments, it reads the four URLs from QGC settings and starts four child processes:

```text
deepSharkVideo1 -> first URL
deepSharkVideo2 -> second URL
deepSharkVideo3 -> third URL
deepSharkVideo4 -> fourth URL
```

To run a single source manually:

```powershell
.\StartYoloToQGC.cmd "rtsp://192.168.2.189:8552/live"
```

To print the auto-detected sources without launching YOLO:

```powershell
.\StartYoloToQGC.cmd --dry-run
```

## Shared YOLO Environment

YOLO dependencies are installed in the shared virtual environment:

```text
D:\Develop\envs\yolo
```

Create and install dependencies:

```powershell
python -m venv D:\Develop\envs\yolo
D:\Develop\envs\yolo\Scripts\python.exe -m pip install --upgrade pip
D:\Develop\envs\yolo\Scripts\python.exe -m pip install -r tools\ai_detection\requirements.txt
```

Default model path:

```text
D:\Develop\envs\yolo\models\yolov8n.pt
```

If a dedicated underwater model is trained later, keep the weights in the shared environment or a project-external model directory, then update `DEFAULT_MODEL` in `tools\ai_detection\run_yolo_to_qgc_udp.bat`.

## Detection Data Format

QGC accepts normalized boxes:

```json
{
  "timestamp": 1710000000.123,
  "source_id": "deepSharkVideo4",
  "detections": [
    {
      "label": "target",
      "confidence": 0.91,
      "x": 0.2,
      "y": 0.15,
      "w": 0.3,
      "h": 0.4
    }
  ]
}
```

It also accepts pixel-space boxes:

```json
{
  "timestamp": 1710000000.123,
  "source_id": "deepSharkVideo4",
  "frame_width": 1280,
  "frame_height": 720,
  "detections": [
    {
      "label": "target",
      "confidence": 0.91,
      "bbox": [256, 108, 640, 396]
    }
  ]
}
```

`source_id` routes detections to the matching video tile. Detections without `source_id` are shown only on the default native QGC video overlay.

## YOLO Overlay Toggle

Toggle locations:

- QGC: `Application Settings -> Video -> YOLO Detection Overlay`
- DeepShark: `DeepShark Video Panel -> Settings -> YOLO Detection Overlay`

When disabled, QGC may still receive UDP detection data, but the UI does not draw boxes.

## ArduSub 4.8, Gripper, and Physical Output Tests

The customized ArduSub 4.8 firmware exposes firmware-backed button functions. The verified continuous Servo12 gripper setup uses `SERVO12_FUNCTION=187` (Actuator4), `BTN9_FUNCTION=116` (`actuator_4_dec`), and `BTN10_FUNCTION=115` (`actuator_4_inc`). Releasing either button keeps the current gripper position; safe PWM travel must be confirmed for each vehicle.

See `docs/research/problem/Servo12机械爪手柄配置说明.md` for full parameters, direction, and speed adjustment. Before any first test, keep the flight controller disarmed, secure the vehicle, and clear the thruster and gripper area.

The three paths in the SERVO output scan tool are intentionally different:

- `Motor Test`: native flight-controller thruster test.
- `Direct Thruster PWM`: sends PWM by physical output number, bypasses `SERVOx_FUNCTION`, and returns to 1500 PWM.
- `Servo Jog`: sends PWM by physical output number and returns to that channel's `SERVOx_TRIM`.

## Flight Controller TCP Diagnosis

The standard serial-server configuration is `TCP Server / 192.168.1.200:4019 / 57600 / 8N1`. A QGC TCP session only proves the network port is reachable; QGC recognizes a vehicle only after MAVLink HEARTBEAT bytes arrive.

For a non-invasive check:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnostics\check-grobot-link.ps1
```

If TCP is established but no vehicle appears, first check flight-controller power, crossed TX/RX plus common ground, the actual serial port's `SERIALx_PROTOCOL=2`, and `SERIALx_BAUD=57`. Do not repeatedly change QGC connection settings. See `docs/audits/flight_controller_connection_incident_2026-07-21.md` for the full incident review.

## Stability Model

- The four streams are independent; one failed stream should not stop the other three.
- Manual Stop disables auto reconnect until the user clicks Start or Reconnect.
- Non-manual failures trigger auto reconnect.
- Watchdog detects channels with no frame progress for an extended period and triggers a controlled reconnect.
- During shutdown, DeepShark tiles stop reconnect timers, watchdog timers, and video controllers to avoid starting video again while QGC is exiting.

To prioritize shutdown stability, the DeepShark video tile FPS and Latency fields may currently show `--`. This does not mean the video is unusable and does not affect YOLO drawing. Multi-RTSP startup can still occasionally leave one channel unavailable on certain GPU, driver, and decoder combinations; use Reconnect All first, or adjust Video Decoder Priority under Application Settings → Video.

## Troubleshooting

### Video does not work after double-clicking the exe

Use the root launcher:

```powershell
.\StartDeepSharkQGC.cmd
```

For an installed release, first ensure that it is not loading DLLs from a Debug build directory; then launch it from the Start menu or installation directory.

### One RTSP channel is unavailable after startup, or a Debug build shows a Qt QML assertion

Use Reconnect All first and record the stuck channel, decoder priority, GPU model, and driver version. Switching temporarily to software decoding is a useful compatibility check. The current work reduces the recurrence rate and exposes a fallback setting; it is not yet a complete root-cause fix.

### The YOLO bridge cannot find Python

Check the shared environment:

```powershell
D:\Develop\envs\yolo\Scripts\python.exe --version
```

If it does not exist, recreate the shared environment and install `tools\ai_detection\requirements.txt`.

### Video is visible in QGC but boxes are missing

Check, in order:

- Is `YOLO Detection Overlay` enabled?
- Is `StartYoloToQGC.cmd` still running?
- Does the bridge console print `sent ... detections to 127.0.0.1:57610`?
- Does the RTSP URL saved in DeepShark settings match the stream being played?
- In auto mode, does the `source_id` match the active tile?

### Boxes are offset or appear over black bars

The overlay computes the real video content area from the video dimensions and draws only inside that area. If offset remains, first verify that the bridge sends `frame_width` and `frame_height` matching the actual inference frame.

### QGC shows Debug CRT heap corruption on shutdown

This is not caused by leaving the YOLO bridge running. The bridge is an external UDP sender; after QGC exits, packets are dropped by the OS. If it still reproduces, continue investigating QGC-side video object lifetime, GStreamer sink release order, or asynchronous reconnect callbacks.

## Development Boundaries

Current project rules:

- Prefer changes under `custom/` and `tools/ai_detection/`.
- Avoid modifying `src/VideoManager/*`.
- Avoid changing MAVLink, FirmwarePlugin, Vehicle, and Comms core paths.
- If a native QGC module must be changed, document the reason, impact, and rollback path.
- Do not perform broad QGC architecture refactors for a single experiment feature.

## Recommended Development Workflow

1. Check `git status --short` before editing.
2. Solve one clearly scoped problem per change.
3. For video UI, prefer `custom/src/DeepShark/`.
4. For AI bridge work, prefer `tools/ai_detection/`.
5. Run a targeted build after code changes.
6. Smoke test startup-sensitive changes with `StartDeepSharkQGC.cmd`.
7. For YOLO changes, verify QGC drawing with `send_sample_detection.py` before running real YOLO.
8. For video changes, test at least one real RTSP source; for stability changes, test two or four sources.

## Suggested Roadmap

1. Continue diagnosing the replacement robot's unpowered flight controller and missing MAVLink byte stream; restore hardware power before checking serial parameters and wiring.
2. Run long-duration DeepShark Video Panel tests with four real RTSP sources and collect Qt/GStreamer compatibility evidence.
3. Use supply-side instruments to investigate weak high-output thruster performance; current flight-controller power telemetry is not reliable.
4. Tune RTSP latency, watchdog thresholds, and reconnect limits using real data.
5. Train or fine-tune an underwater target model to replace the default COCO `yolov8n.pt`.
6. Add recording, log export, gimbal control, or manipulator control after the core operation loop is stable.
