# QGC_KevinJiang

QGC_KevinJiang is an internal GRobot ground control application maintained by Kevin Jiang. It is based on the open-source QGroundControl project and customized for DeepShark multi-camera operation and optional local AI detection.

- Project name: QGC_KevinJiang
- Current stable version: 1.4.0
- Development candidate: 2.0.0 Debug, based on QGroundControl v5.1.5
- Stable v1.4.0 upstream base: QGroundControl v5.0.8
- Maintainer: Jiang Zhongze / KevinJiang1018@gmail.com
- Distribution: Windows installer published through GitHub Releases
- Development branch: `main`; single-maintainer branch and directory rules are documented in [Repository maintenance](docs/development/repository-maintenance.md).

## Main Features

- DeepShark four-channel RTSP video panel for front, left, right, and rear camera views.
- Optional YOLO AI detection overlay using a local Python environment and local model files.
- Built-in AI environment check and AI start/stop/restart controls in QGC video settings.
- GRobot application icon and Windows NSIS installer packaging.
- QGC native video source defaults to disabled and respects the disabled state when vehicle video stream information is received.
- ArduSub 4.8 parameter metadata and firmware-backed joystick actions for Servo12/Actuator4 gripper control.
- Separate physical-output test paths for thruster PWM and servo jog operation.

## Installation

Download the Windows installer from the GitHub Release page for the required version. For v1.4.0, use:

- `QGC_v1.4.0_KevinJiang-installer.exe`

The installer is distributed as a Release asset. It is intentionally not committed into the normal source tree.

## Optional AI Detection

AI detection is optional. The installer includes the lightweight AI bridge scripts, but it does not bundle large Python dependencies, CUDA libraries, or YOLO model files.

Each operator should configure:

- Python executable path for a local environment with `ultralytics` and `torch`.
- YOLO model path, for example a local `.pt` file.
- Optional device value, such as `cpu`, `0`, or `cuda:0`.

Use the environment check in the Video settings page before starting AI detection.

## Source Provenance

The development source is based on the complete official QGroundControl v5.1.5 project. DeepShark functionality and necessary core fixes are adapted to that version. The existing stable v1.4.0 release remains based on v5.0.8. The upstream QGroundControl history is preserved for traceability and license compliance.

Upstream project:

- https://github.com/mavlink/qgroundcontrol

## Documentation

- Repository and directory maintenance: [docs/development/repository-maintenance.md](docs/development/repository-maintenance.md)
- Release notes: [docs/releases/v1.4.0.md](docs/releases/v1.4.0.md)
- Original QGroundControl user documentation: https://docs.qgroundcontrol.com/
- Original QGroundControl developer documentation: https://dev.qgroundcontrol.com/

## License

QGroundControl is licensed according to the license files included in this repository. QGC_KevinJiang keeps the upstream licensing and attribution.

## Upstream QGroundControl

<p align="center">
  <img src="https://raw.githubusercontent.com/Dronecode/UX-Design/35d8148a8a0559cd4bcf50bfa2c94614983cce91/QGC/Branding/Deliverables/QGC_RGB_Logo_Horizontal_Positive_PREFERRED/QGC_RGB_Logo_Horizontal_Positive_PREFERRED.svg" alt="QGroundControl Logo" width="500">
</p>

<p align="center">
  <a href="https://github.com/mavlink/QGroundControl/releases"><img src="https://img.shields.io/github/v/release/mavlink/QGroundControl" alt="Latest Release"></a>
  <a href="https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md"><img src="https://img.shields.io/github/license/mavlink/QGroundControl" alt="License"></a>
  <a href="https://github.com/mavlink/QGroundControl/actions/workflows/linux.yml"><img src="https://github.com/mavlink/QGroundControl/actions/workflows/linux.yml/badge.svg" alt="Linux Build"></a>
  <a href="https://securityscorecards.dev/viewer/?uri=github.com/mavlink/qgroundcontrol"><img src="https://img.shields.io/ossf-scorecard/github.com/mavlink/qgroundcontrol?label=openssf%20scorecard" alt="OpenSSF Scorecard"></a>
  <a href="https://crowdin.com/project/qgroundcontrol"><img src="https://badges.crowdin.net/qgroundcontrol/localized.svg" alt="Crowdin"></a>
  <a href="https://discord.com/channels/1022170275984457759/1022185820683255908"><img src="https://img.shields.io/discord/1022170275984457759?logo=discord&logoColor=white&label=Discord" alt="Dronecode Discord"></a>
  <a href="https://doi.org/10.5281/zenodo.595404"><img src="https://zenodo.org/badge/DOI/10.5281/zenodo.595404.svg" alt="DOI"></a>
</p>

**QGroundControl** (QGC) is a Ground Control Station (GCS) for UAVs, providing full flight control
and mission planning for any *MAVLink-enabled* drone, including *PX4* and *ArduPilot* platforms.

## Features

- **Mission planning** — plan, edit, and fly autonomous waypoint, survey, and structure-scan missions.
- **Live Fly View** — real-time flight display with map, instruments, and full vehicle telemetry.
- **Vehicle setup** — guided wizards for sensor calibration, radio, flight modes, and power.
- **Parameter tuning** — inspect and edit every vehicle parameter through the Fact System.
- **Video streaming** — GStreamer-based UDP RTP / RTSP video with recording in the Flight Display.
- **Multi-vehicle** — connect to and monitor multiple vehicles simultaneously.
- **MAVLink tooling** — built-in MAVLink Inspector, console, and log download/analysis.
- **Cross-platform** — Windows, macOS, Linux, Android, and iOS from a single codebase.

## Download

Grab the latest stable build for your platform, or see all assets on the
[releases page](https://github.com/mavlink/QGroundControl/releases/latest):

<p align="center">
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl-installer.exe"><img src="https://img.shields.io/badge/Windows-0078D6?logo=windows&logoColor=white" alt="Windows"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl.dmg"><img src="https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white" alt="macOS"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl-x86_64.AppImage"><img src="https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black" alt="Linux (AppImage)"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl.apk"><img src="https://img.shields.io/badge/Android-3DDC84?logo=android&logoColor=white" alt="Android"></a>
</p>

## Links

- [Official Website](http://qgroundcontrol.com)
- [User Manual](https://docs.qgroundcontrol.com/en/)
- [Developer Guide](https://dev.qgroundcontrol.com/en/) / [Build Instructions](https://dev.qgroundcontrol.com/en/getting_started/)
- [Discussion & Support](https://docs.qgroundcontrol.com/en/Support/Support.html)
- [Dronecode Discord](https://discord.com/channels/1022170275984457759/1022185820683255908)
- [Security Policy](.github/SECURITY.md)
- [Code of Conduct](.github/CODE_OF_CONDUCT.md)
- [License](https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md)

## Contributing

QGC is open source and welcomes contributions. See [AGENTS.md](AGENTS.md) for build/test/lint
commands and coding conventions, and [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) for
architecture patterns and the contribution workflow.

QGC's interface is translated by the community — help translate it into your language on
[Crowdin](https://crowdin.com/project/qgroundcontrol).

## Star history

[![Star History Chart](https://api.star-history.com/svg?repos=mavlink/qgroundcontrol&type=Date)](https://star-history.com/#mavlink/qgroundcontrol&Date)
