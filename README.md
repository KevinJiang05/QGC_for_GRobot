# QGC for GRobot

**English** | [简体中文](README.zh-CN.md)

A ground control station for the DeepShark underwater robot, built on the open-source [QGroundControl](https://github.com/mavlink/qgroundcontrol) and customized by Kevin Jiang with four-channel RTSP video, optional local YOLO detection, a disconnect alert, and ArduSub setup and output-test tools.

**[Download v2.1.0 (Windows)](https://github.com/KevinJiang05/QGC_for_GRobot/releases/tag/qgc-kevinjiang-v2.1.0)** · **[All releases](https://github.com/KevinJiang05/QGC_for_GRobot/releases)** · **[Project documentation](docs-deepshark/README.md)** · **[Release notes (2.1.0)](docs-deepshark/releases/v2.1.0.zh-CN.md)**

The application and its installer are named **QGC_KevinJiang**.

<!--
Screenshot placeholder: add one or two screenshots here, then replace this comment with e.g.
  <p align="center"><img src="docs-deepshark/images/four-video-panel.png" alt="Four-channel RTSP video panel" width="900"></p>
Suggested shots (save under docs-deepshark/images/):
  1. four-video-panel.png - Fly View with the four-channel RTSP video panel (front/left/right/rear)
  2. ai-detection-overlay.png - a video tile with the YOLO detection overlay drawn
  3. connection-alert.png - the disconnect alert banner (heartbeat or video lost)
Before committing, check the images for device IP addresses, personal paths, or other private details.
-->

## Main Features

- DeepShark four-channel RTSP video panel for front, left, right, and rear camera views, with grid, main-plus-auxiliary, panorama, and single-camera fullscreen layouts and automatic reconnection.
- Optional YOLO AI detection overlay using a local Python environment and local model files.
- Built-in AI environment check and AI start/stop/restart controls in QGC video settings.
- Disconnect alert that watches the vehicle heartbeat and the video frames and warns when either link stops.
- 3D attitude and depth panel, and an AUV mission preview panel that imports QGC `.plan` files.
- Arm/disarm and heartbeat status on the main toolbar; joystick setup in four pages (general, button assignment, calibration, advanced).
- ArduSub 4.8 parameter metadata and firmware-backed joystick actions for Servo12/Actuator4 gripper control.
- Physical-output tool for thruster PWM and servo jog tests, with output backup, recovery and neutral confirmation; it refuses to start while an arm request is still pending.
- GRobot application icon and Windows NSIS installer with in-place upgrade that keeps user settings.

## How It Works

**Multi-camera video.** Each camera tile has its own controller (`DeepSharkVideoController`) on top of QGroundControl's GStreamer video receiver. It selects the RTSP transport, offers a low-latency mode, retries when a stream drops, and reports resolution, frame rate, and estimated latency per stream. Stream addresses are shown without their user name and password.

**AI detection bridge.** QGC starts and supervises one Python process (`AIDetectionManager`) that reads all enabled camera sources and runs a single YOLO model. The bridge keeps only the latest frame of each source and limits the inference rate, so a slow model does not build up delay. Detections come back to QGC as JSON over UDP (`AIDetectionReceiver`) and are drawn on the tile of the matching source. The environment check runs as a separate process before detection starts.

**Disconnect alert.** `DeepSharkConnectionMonitor` is owned by the application rather than by an on-screen overlay, so hiding or reloading the overlay cannot silence it. It tracks MAVLink `HEARTBEAT` messages and decoded video frames against configurable timeouts, and raises a banner and optional sound that the operator acknowledges.

**Safe output testing.** The output tool (`ThrusterDirectControlController`) only starts a test while the vehicle keeps reporting its output values, so it can confirm that the outputs went back to neutral. It records the original output function before a test and restores it afterwards. Relaxing these checks requires the operator to switch off the setup safety restrictions explicitly.

**Telemetry that has not arrived yet.** Attitude and depth start at 0 in QGC, which looks the same as "level at the surface". `DeepSharkTelemetryArrival` tracks whether the vehicle has actually reported them, and the 3D attitude panel shows no value until it has.

DeepShark-specific unit and UI tests live in [test/DeepShark/](test/DeepShark/).

## Tech Stack

- C++ and QML on Qt 6 (built with Qt 6.11.1, MSVC 2022, CMake and Ninja)
- GStreamer for RTSP video decoding
- MAVLink with ArduSub firmware
- Python, Ultralytics YOLO and PyTorch for the optional detection bridge
- NSIS for the Windows installer; MediaMTX as a local RTSP server for development

## Repository Layout

| Path | Contents |
| --- | --- |
| [custom/](custom/) | DeepShark plugin: C++ controllers in `custom/src/`, QML panels in `custom/src/DeepShark/` |
| [test/DeepShark/](test/DeepShark/) | Tests for the DeepShark controllers and UI layout |
| [tools/ai_detection/](tools/ai_detection/) | Python YOLO bridge that sends detections to QGC |
| [tools/rtsp/](tools/rtsp/) | MediaMTX configuration for a local RTSP server |
| [tools/debug/](tools/debug/), [tools/release/](tools/release/) | Windows Debug build and release packaging scripts |
| [docs-deepshark/](docs-deepshark/) | Project documentation and release notes |
| `src/`, `docs/` and other directories | Upstream QGroundControl source and documentation |

Changes to upstream files are listed in [upstream patches](docs-deepshark/upstream-patches.md).

## Installation

Download `QGC_v<version>_KevinJiang-installer.exe` from [GitHub Releases](https://github.com/KevinJiang05/QGC_for_GRobot/releases). The latest release is [2.1.0](https://github.com/KevinJiang05/QGC_for_GRobot/releases/tag/qgc-kevinjiang-v2.1.0), based on QGroundControl v5.1.5. Releases 1.5.0 and earlier are based on QGroundControl v5.0.8 and lack some of the features above; check the release notes for the version you install. The installers are not code-signed, so Windows SmartScreen may warn before running them.

To upgrade, quit QGC and run the new installer into the existing directory; uninstalling first is not needed. The release notes for each version describe backup and rollback.

Installers are not committed into the source tree.

## Optional AI Detection

AI detection is optional. The installer includes the lightweight AI bridge scripts, but it does not bundle large Python dependencies, CUDA libraries, or YOLO model files.

Each operator should configure:

- Python executable path for a local environment with `ultralytics` and `torch`.
- YOLO model path, for example a local `.pt` file.
- Optional device value, such as `cpu`, `0`, or `cuda:0`.

Use the environment check in the Video settings page before starting AI detection. Bridge details are in [tools/ai_detection/README.md](tools/ai_detection/README.md).

## Building from Source

The project is developed and built on Windows x64 with Qt 6.11.1 (MSVC 2022) and GStreamer 1.28.4. Configuration, the incremental build script `tools/debug/build-windows-debug.ps1`, tests, and static checks are described in the [Windows Debug build guide](docs-deepshark/debug-build.md) (Chinese); release packaging is described in the [release runbook](docs-deepshark/release-runbook.md). The guides use the maintainer's local dependency paths, so adjust them to your environment.

## Versions and Development

- Latest release: 2.1.0, based on QGroundControl v5.1.5 ([release notes](docs-deepshark/releases/v2.1.0.zh-CN.md)); 1.x releases are based on QGroundControl v5.0.8
- Development: `main` carries 2.1.0; the Debug build shows `2.1.0 Debug (QGroundControl v5.1.5)`
- Single-maintainer branch and directory rules are documented in [Repository maintenance](docs-deepshark/repository.md).
- Maintainer: Jiang Zhongze (Kevin Jiang) / <KevinJiang1018@gmail.com>

## Documentation

- Project documentation index: [docs-deepshark/README.md](docs-deepshark/README.md)
- Repository and directory maintenance: [docs-deepshark/repository.md](docs-deepshark/repository.md)
- Release notes: [v2.1.0](docs-deepshark/releases/v2.1.0.zh-CN.md), [v1.4.0](docs-deepshark/releases/v1.4.0.md), all versions in [docs-deepshark/releases/](docs-deepshark/releases/)
- Original QGroundControl user documentation: <https://docs.qgroundcontrol.com/>
- Original QGroundControl developer documentation: <https://dev.qgroundcontrol.com/>

## Based on QGroundControl

[QGroundControl](https://github.com/mavlink/qgroundcontrol) (QGC) is an open-source ground control station for MAVLink vehicles such as PX4 and ArduPilot. It provides the base of this project: telemetry, parameter management, vehicle setup, and video streaming. This repository is a customized distribution, not an official QGroundControl release; for the official application and downloads, see the [upstream project](https://github.com/mavlink/qgroundcontrol).

## Source Provenance

The development source is based on the complete official QGroundControl v5.1.5 project. DeepShark functionality and necessary core fixes are adapted to that version. Releases 1.5.0 and earlier remain based on v5.0.8. The upstream QGroundControl history is preserved for traceability and license compliance.

Upstream project:

- <https://github.com/mavlink/qgroundcontrol>

## License

QGroundControl is licensed according to the license files included in this repository ([.github/COPYING.md](.github/COPYING.md), [LICENSE-APACHE](LICENSE-APACHE), [LICENSE-GPL](LICENSE-GPL)). QGC_KevinJiang keeps the upstream licensing and attribution.
