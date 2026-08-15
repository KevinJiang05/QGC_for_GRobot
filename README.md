# QGC_KevinJiang

QGC_KevinJiang is an internal GRobot ground control application maintained by Kevin Jiang. It is based on the open-source QGroundControl project and customized for DeepShark multi-camera operation and optional local AI detection.

- Project name: QGC_KevinJiang
- Current stable version: 1.3.1
- Upstream base: QGroundControl v5.0.8
- Maintainer: Jiang Zhongze / KevinJiang1018@gmail.com
- Distribution: Windows installer published through GitHub Releases

## Main Features

- DeepShark four-channel RTSP video panel for front, left, right, and rear camera views.
- Optional YOLO AI detection overlay using a local Python environment and local model files.
- Built-in AI environment check and AI start/stop/restart controls in QGC video settings.
- GRobot application icon and Windows NSIS installer packaging.
- QGC native video source defaults to disabled and respects the disabled state when vehicle video stream information is received.
- ArduSub 4.8 parameter metadata and firmware-backed joystick actions for Servo12/Actuator4 gripper control.
- Separate physical-output test paths for thruster PWM and servo jog operation.

## Installation

Download the Windows installer from the GitHub Release page for the required version. For v1.3.1, use:

- `QGC_v1.3.1_KevinJiang-installer.exe`

The installer is distributed as a Release asset. It is intentionally not committed into the normal source tree.

## Optional AI Detection

AI detection is optional. The installer includes the lightweight AI bridge scripts, but it does not bundle large Python dependencies, CUDA libraries, or YOLO model files.

Each operator should configure:

- Python executable path for a local environment with `ultralytics` and `torch`.
- YOLO model path, for example a local `.pt` file.
- Optional device value, such as `cpu`, `0`, or `cuda:0`.

Use the environment check in the Video settings page before starting AI detection.

## Source Provenance

This project is a secondary development based on QGroundControl v5.0.8. The upstream QGroundControl history is intentionally preserved for traceability and license compliance.

Upstream project:

- https://github.com/mavlink/qgroundcontrol

## Documentation

- Release notes: [docs/releases/v1.3.1.md](docs/releases/v1.3.1.md)
- Original QGroundControl user documentation: https://docs.qgroundcontrol.com/
- Original QGroundControl developer documentation: https://dev.qgroundcontrol.com/

## License

QGroundControl is licensed according to the license files included in this repository. QGC_KevinJiang keeps the upstream licensing and attribution.
