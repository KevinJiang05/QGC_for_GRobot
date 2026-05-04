# QGC_GRobot / DeepShark Ground Station

This repository is a **QGroundControl v5.0.8 Stable** based custom ground station for an underwater robotics competition project. The current strategy is “Plan A”: keep the native QGC ArduPilot / MAVLink / parameter / flight mode / telemetry / control stack intact, and add the DeepShark competition UI through the official QGC custom build mechanism.

The first version focuses on reliable operation and low-latency multi-RTSP viewing. It does not refactor the native QGC `VideoManager`, does not modify MAVLink definitions, and does not replace the ArduPilot/APM firmware plugin.

## Current Features

- QGC custom build is enabled and loads `DeepSharkPlugin`.
- Fly View loads a custom `FlyViewCustomLayer.qml`.
- Four RTSP video channels.
- Custom names and RTSP URLs for all four channels.
- Layouts: 2x2 grid, main + auxiliary, in-panel fullscreen, minimized panel.
- Video Main Mode hides and disables the underlying map to reduce rendering and interaction overhead.
- QGC top connection/status bar and key lower/right-side flight instruments are preserved.
- Independent Start / Stop / Reconnect per video channel.
- Reconnect All restarts channels sequentially to avoid a resource spike.
- Per-channel state machine: `Waiting`, `Connecting`, `Streaming`, `Playing`, `Stopped`, `Failed`, `Reconnecting`, `Stalled`.
- Auto reconnect and watchdog-based freeze detection.
- Resolution, FPS, watchdog status, and estimated receiver-side latency display.
- Right-side DeepShark Status Panel with system state, video state, RTSP URLs, and recent events.

## Architecture

DeepShark changes are intentionally concentrated under `custom/`.

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
    FlyViewCustomLayer.qml
    DeepShark/
      FourVideoPanel.qml
      VideoTile.qml
      DeepSharkStatusPanel.qml
```

### Custom Build Entry

`custom/CMakeLists.txt` sets:

- `QGC_CUSTOM_BUILD`
- `CUSTOMHEADER="DeepSharkPlugin.h"`
- `CUSTOMCLASS=DeepSharkPlugin`
- `custom/custom.qrc`
- DeepShark C++ sources
- include paths for `custom/src` and QGC `VideoReceiver`

`custom/cmake/CustomOverrides.cmake` is intentionally minimal. It does not disable APM, does not disable MAVLink, and does not alter the firmware plugin factory.

### QML Override

`custom/custom.qrc` uses the `/Custom/qml` prefix to override the Fly View custom layer:

```xml
<file alias="QGroundControl/FlightDisplay/FlyViewCustomLayer.qml">
    src/FlyViewCustomLayer.qml
</file>
```

`DeepSharkPlugin` uses the custom override/interceptor path so QGC loads the custom Fly View layer from the custom resource path. This inserts DeepShark UI without editing `src/FlightDisplay/FlyView.qml`.

### Fly View Custom Layer

`custom/src/FlyViewCustomLayer.qml` is the top-level DeepShark UI entry point. It handles:

- Showing/hiding the DeepShark Video Panel.
- Showing/hiding the DeepShark Status Panel.
- Video Main Mode.
- Hiding and disabling `mapControl` in Video Main Mode.
- Recent Events.
- Preserving QGC `parentToolInsets` / `totalToolInsets` forwarding.

### Video Panel

`custom/src/DeepShark/FourVideoPanel.qml` manages the four-channel video layout and interactions:

- `grid`: 2x2 overview.
- `mainAux`: one main view with three auxiliary views.
- `fullscreen`: one video maximized inside the DeepShark panel.
- `minimized`: DeepShark panel hidden, native QGC map restored.

Layout switching avoids destroying `VideoTile` instances, so RTSP streams should not reconnect just because the layout changed.

### Video Tile

`custom/src/DeepShark/VideoTile.qml` owns a single video channel UI and state machine:

- One independent `DeepSharkVideoController`.
- Independent Start / Stop / Reconnect.
- Independent retry count, lastError, and watchdog state.
- Resolution, FPS, estimated latency, state, and watchdog age display.

### Video Controller

`custom/src/DeepSharkVideoController.h/.cc` is the DeepShark video integration layer.

It creates native QGC video objects through the core plugin:

- `QGCCorePlugin::createVideoReceiver`
- `QGCCorePlugin::createVideoSink`
- `QGCCorePlugin::releaseVideoSink`

This reuses QGC’s existing GStreamer / VideoReceiver stack without modifying `src/VideoManager/*`.

The controller also installs a lightweight GStreamer sink pad probe to collect:

- frame count
- FPS
- resolution
- estimated receiver-side latency

Latency is estimated from sink buffer PTS and the pipeline clock. It is useful for detecting receiver-side buffering or lag, but it is not a true camera-to-screen end-to-end latency measurement.

### Video Settings

`custom/src/DeepSharkVideoSettings.h/.cc` stores four channel names and RTSP URLs in `QSettings`.

After saving settings:

- Channel names are updated.
- RTSP URLs are updated.
- Video Panel and Status Panel are synchronized.
- Empty URLs put the channel into `Waiting`.
- Changed URLs restart only the affected channel.
- Unchanged settings should not cause unnecessary reconnects.

## Windows Development Environment

The current project targets Windows.

Verified local paths:

```text
Qt:         D:\Develop\Qt\6.8.3\msvc2022_64
Visual Studio: D:\Develop\Vs2022\Community
GStreamer: D:\gstreamer\1.0\msvc_x86_64
Build dir: build
```

On another machine, paths may differ, but the expected stack is:

- Qt 6.8.x MSVC 64-bit
- Visual Studio 2022 C++ toolchain
- CMake
- Ninja
- GStreamer MSVC x86_64 runtime/development package

## Build

Run from the repository root:

```powershell
cmd.exe /c "call D:\Develop\Vs2022\Community\VC\Auxiliary\Build\vcvars64.bat >nul && cmake --build build --config Debug"
```

If the linker reports:

```text
LINK : fatal error LNK1168: cannot open Debug\QGroundControl.exe for writing
```

QGC is still running and the exe is locked. Stop it first:

```powershell
Get-Process QGroundControl -ErrorAction SilentlyContinue | Stop-Process
```

Then build again.

## Run

Do not run the video-enabled build by double-clicking `build\Debug\QGroundControl.exe`. RTSP/GStreamer requires environment variables. Use:

```powershell
D:\Develop\QGC_for_GRobot\StartDeepSharkQGC.cmd
```

The script sets:

- `PATH`
- `GST_PLUGIN_PATH`
- `GST_PLUGIN_SYSTEM_PATH`
- `GST_PLUGIN_SCANNER`
- `GIO_EXTRA_MODULES`

Then it starts the Debug QGC executable.

## RTSP Usage

After QGC starts, open Fly View. The DeepShark Video Panel is shown by default.

Common actions:

- `Settings`: edit names and RTSP URLs for all four channels.
- `Start`: start the current/default channel.
- `Stop`: manually stop video. Manual stop disables auto reconnect.
- `Reconnect`: reconnect the selected channel.
- `Reconnect All`: sequentially reconnect all four channels.
- `Grid`: return to the 2x2 overview.
- `Main`: enter main + auxiliary layout.
- `Fullscreen`: enlarge the selected channel inside the DeepShark panel.
- `Minimize`: hide the DeepShark Panel and restore the native QGC map view.
- `Show Map`: toggle map visibility.

Before adding a source to DeepShark, verify it with VLC or PotPlayer:

```text
rtsp://<ip>:<port>/<path>
```

## Stability Model

### Independent Channels

The four video channels are independent. A bad URL, stream failure, or reconnect on one channel should not stop the other three.

### Auto Reconnect

Failures not caused by a manual Stop trigger auto reconnect:

- About 3 seconds after the first failure.
- About 5 seconds for later attempts.
- After the retry limit, the channel enters `Failed` and waits for manual Reconnect.

### Manual Stop

After the user clicks Stop:

- State becomes `Stopped`.
- Auto reconnect is disabled.
- Watchdog reconnect is disabled.
- The user must Start/Reconnect manually.

### Watchdog

Each channel uses sink frame count to detect real frame progress.

If a channel appears to be playing but no new frame arrives for about 8 seconds:

- The state becomes `Stalled`.
- A Recent Event is recorded.
- A controlled reconnect is triggered.

Watchdog reconnects are throttled to avoid tight restart loops.

### Latency Display

Each channel displays `Latency: xx ms` or `Latency: --`.

This is receiver-side estimated latency:

- Based on GStreamer buffer PTS and the pipeline clock.
- Useful for detecting receiver buffering or lag.
- Not a true camera-to-screen end-to-end latency.
- If the RTSP source does not provide valid PTS, it shows `--`.

## Status Panel

The DeepShark Status Panel displays:

- Current layout mode.
- Current main view.
- Map state.
- DeepShark Panel state.
- Vehicle state placeholder/basic state.
- Per-channel state, retry, watchdog, FPS, latency, and lastError.
- Four RTSP URLs.
- Recent Events.

Recent Events is scrollable. It records events available inside the DeepShark custom layer and does not read QGC global log files.

## Development Boundaries

Current project rules:

- Prefer changes under `custom/`.
- Do not edit `src/FlightDisplay/FlyView.qml`.
- Do not edit `src/FlightDisplay/FlyViewWidgetLayer.qml`.
- Do not edit `src/VideoManager/*`.
- Do not edit `src/MAVLink/*`.
- Do not edit `src/FirmwarePlugin/APM/*`.
- Do not edit `src/Vehicle/*`.
- Do not edit `src/Comms/*`.
- Do not alter ArduPilot/APM plugin configuration.
- Do not modify MAVLink protocol definitions.
- Do not refactor native QGC video architecture.

If a future task truly requires touching native QGC modules, document the reason, impact, and rollback plan before making small scoped changes.

## Recommended Development Workflow

1. Run `git status --short` before changing files.
2. Solve one problem per change.
3. Prefer `custom/`.
4. Build after changes.
5. For startup-sensitive changes, smoke test with `StartDeepSharkQGC.cmd`.
6. For video changes, test with at least one RTSP source.
7. For stability changes, test with two/four RTSP sources.
8. Commit each stage separately with a clear scope.

## Troubleshooting

### Double-clicking the exe does nothing or GStreamer plugins are missing

Use `StartDeepSharkQGC.cmd` instead of launching the exe directly.

### Build cannot overwrite the exe

QGC is still running. Stop it and build again:

```powershell
Get-Process QGroundControl -ErrorAction SilentlyContinue | Stop-Process
```

### VLC works, DeepShark does not

Check:

- Was QGC started through `StartDeepSharkQGC.cmd`?
- Is the RTSP URL saved to the correct channel?
- Is GStreamer installed at the path used by the script?
- Are the camera/phone and PC on the same network?
- Is Windows Firewall blocking traffic?
- Does the RTSP source include audio, use H.264/H.265, or limit clients?

### Latency shows `--`

The stream may not provide valid PTS, or the pipeline is not decoding yet. This does not always mean the video is broken.

## Current Limitations

- Real robot integration has not yet been completed.
- Vehicle status in the Status Panel is still placeholder/basic and is not deeply bound to active vehicle data yet.
- No video recording.
- No gimbal control.
- No manipulator control.
- No 3D attitude widget yet.
- Latency is receiver-side estimated latency, not true end-to-end latency.
- Status Panel does not read QGC global logs.

## Suggested Roadmap

Recommended priorities:

1. Connect to the real ArduPilot / MAVLink vehicle in the lab and validate telemetry, modes, arming, and safety control paths.
2. Long-duration stability tests with four real RTSP sources.
3. Tune RTSP latency, watchdog thresholds, and reconnect limits using real data.
4. Add a lightweight 3D attitude widget bound to active vehicle roll / pitch / yaw.
5. Harden competition UI: reduce accidental touches, lock layouts, add quick recovery actions.
6. Add logging export, recording, or camera control only after the core operation loop is stable.

