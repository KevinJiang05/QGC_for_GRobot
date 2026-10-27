# QGC for GRobot

[English](README.md) | **简体中文**

DeepShark 水下机器人的地面站，基于开源的 [QGroundControl](https://github.com/mavlink/qgroundcontrol)，由 Kevin Jiang 定制：四路 RTSP 视频、可选的本地 YOLO 检测、断连告警，以及 ArduSub 设置与输出测试工具。

**[下载 v2.1.0（Windows）](https://github.com/KevinJiang05/QGC_for_GRobot/releases/tag/qgc-kevinjiang-v2.1.0)** · **[全部版本](https://github.com/KevinJiang05/QGC_for_GRobot/releases)** · **[项目文档](docs-deepshark/README.md)** · **[发布说明（2.1.0）](docs-deepshark/releases/v2.1.0.zh-CN.md)**

程序和安装包的名称是 **QGC_KevinJiang**。

<!--
截图占位：在这里放一到两张截图，然后把本注释换成例如
  <p align="center"><img src="docs-deepshark/images/four-video-panel.png" alt="四路 RTSP 视频面板" width="900"></p>
建议截图（放在 docs-deepshark/images/，与英文 README 共用）：
  1. four-video-panel.png - 飞行界面中的四路 RTSP 视频面板（前、左、右、后）
  2. ai-detection-overlay.png - 带 YOLO 检测框叠加的视频画面
  3. connection-alert.png - 断连告警横幅（心跳或视频中断）
提交前检查图片中是否有设备 IP、个人路径等隐私信息。
-->

## 主要功能

- DeepShark 四路 RTSP 视频面板，显示前、左、右、后四个摄像头；支持网格、主副画面、全景和单路全屏布局，断流后自动重连。
- 可选的 YOLO AI 检测叠加，使用本地 Python 环境和本地模型文件。
- 在 QGC 视频设置中内置 AI 环境检查，以及 AI 启动、停止、重启控制。
- 断连告警：同时监视载具心跳和视频帧，任一链路中断时提醒。
- 3D 姿态与深度面板，以及可导入 QGC `.plan` 文件的 AUV 任务预览面板。
- 主工具栏显示解锁状态和心跳状态；手柄设置分为四页（常规、按键分配、校准、高级）。
- ArduSub 4.8 参数元数据，以及由固件执行的 Servo12/Actuator4 机械爪手柄动作。
- 物理输出工具，用于推进器 PWM 和舵机点动测试，带输出备份、恢复和回中确认；解锁请求尚未完成时拒绝启动。
- GRobot 应用图标，以及支持覆盖升级、保留用户设置的 Windows NSIS 安装器。

## 实现要点

**多路视频。** 每个摄像头画面有独立的控制器（`DeepSharkVideoController`），建立在 QGroundControl 的 GStreamer 视频接收器之上。它负责选择 RTSP 传输方式、提供低延迟模式、断流后重试，并分别显示每路的分辨率、帧率和估计延迟。界面上显示的流地址不含用户名和密码。

**AI 检测桥接。** QGC 启动并监管一个 Python 进程（`AIDetectionManager`），由它读取所有已启用的摄像头源，共用一个 YOLO 模型。桥接程序对每个源只保留最新一帧，并限制推理频率，模型慢时也不会越积越久。检测结果以 JSON 经 UDP 发回 QGC（`AIDetectionReceiver`），画在对应源的画面上。环境检查在检测启动前以单独的进程运行。

**断连告警。** `DeepSharkConnectionMonitor` 归应用本身所有，而不挂在界面叠加层上，所以隐藏或重新加载叠加层不会让告警失效。它按可配置的超时监视 MAVLink `HEARTBEAT` 消息和已解码的视频帧，超时后弹出横幅、可选播放声音，由操作员确认。

**安全的输出测试。** 输出工具（`ThrusterDirectControlController`）只在载具持续上报输出值时才开始测试，以便确认输出已回到中位。测试前记录原来的输出功能，测试后恢复。放宽这些检查必须由操作员明确关闭"设置安全限制"。

**区分"还没收到"的遥测。** QGC 中姿态和深度的初始值为 0，看起来与"水面上水平静止"一样。`DeepSharkTelemetryArrival` 记录载具是否真正上报过这些数据，3D 姿态面板在收到之前不显示数值。

DeepShark 专属的单元测试和界面测试在 [test/DeepShark/](test/DeepShark/)。

## 技术栈

- 基于 Qt 6 的 C++ 和 QML（使用 Qt 6.11.1、MSVC 2022、CMake 和 Ninja 构建）
- GStreamer 解码 RTSP 视频
- MAVLink，配合 ArduSub 固件
- 可选检测桥接使用 Python、Ultralytics YOLO 和 PyTorch
- Windows 安装器使用 NSIS；开发时用 MediaMTX 作本地 RTSP 服务器

## 目录结构

| 路径 | 内容 |
| --- | --- |
| [custom/](custom/) | DeepShark 插件：C++ 控制器在 `custom/src/`，QML 面板在 `custom/src/DeepShark/` |
| [test/DeepShark/](test/DeepShark/) | DeepShark 控制器和界面布局的测试 |
| [tools/ai_detection/](tools/ai_detection/) | 向 QGC 发送检测结果的 Python YOLO 桥接程序 |
| [tools/rtsp/](tools/rtsp/) | 本地 RTSP 服务器的 MediaMTX 配置 |
| [tools/debug/](tools/debug/)、[tools/release/](tools/release/) | Windows Debug 构建和正式打包脚本 |
| [docs-deepshark/](docs-deepshark/) | 项目文档和发布说明 |
| `src/`、`docs/` 等其余目录 | 上游 QGroundControl 源码和文档 |

对上游文件的修改登记在[上游补丁清单](docs-deepshark/upstream-patches.md)。

## 安装

从 [GitHub Releases](https://github.com/KevinJiang05/QGC_for_GRobot/releases) 下载 `QGC_v<版本号>_KevinJiang-installer.exe`。最新版本是 [2.1.0](https://github.com/KevinJiang05/QGC_for_GRobot/releases/tag/qgc-kevinjiang-v2.1.0)，基于 QGroundControl v5.1.5。1.5.0 及更早的版本基于 QGroundControl v5.0.8，没有上面的部分功能，请查看所装版本的发布说明。安装包没有代码签名，运行前 Windows SmartScreen 可能会提示风险。

升级时先退出 QGC，再把新安装包安装到原目录即可，不必先卸载。各版本的发布说明写有备份和回退方法。

安装包不提交到源码仓库。

## 可选的 AI 检测

AI 检测是可选功能。安装包带有轻量的 AI 桥接脚本，但不包含体积较大的 Python 依赖、CUDA 库或 YOLO 模型文件。

每位操作员需要自行配置：

- 本地 Python 环境的可执行文件路径，环境中需装有 `ultralytics` 和 `torch`。
- YOLO 模型路径，例如本地的 `.pt` 文件。
- 可选的设备参数，如 `cpu`、`0` 或 `cuda:0`。

启动 AI 检测前，先在视频设置页运行环境检查。桥接程序的详细说明见 [tools/ai_detection/README.md](tools/ai_detection/README.md)。

## 从源码构建

本项目在 Windows x64 上开发和构建，使用 Qt 6.11.1（MSVC 2022）和 GStreamer 1.28.4。配置方法、增量编译脚本 `tools/debug/build-windows-debug.ps1`、测试和静态检查见 [Windows Debug 构建说明](docs-deepshark/debug-build.md)；正式打包见[发布流程](docs-deepshark/release-runbook.md)。说明中使用的是维护者本机的依赖路径，请按自己的环境调整。

## 版本与开发

- 最新版本：2.1.0，基于 QGroundControl v5.1.5（[发布说明](docs-deepshark/releases/v2.1.0.zh-CN.md)）；1.x 版本基于 QGroundControl v5.0.8
- 开发：`main` 分支为 2.1.0；Debug 构建显示 `2.1.0 Debug (QGroundControl v5.1.5)`
- 单人维护的分支与目录规则见[仓库维护说明](docs-deepshark/repository.md)。
- 维护者：Jiang Zhongze（Kevin Jiang）/ <KevinJiang1018@gmail.com>

## 文档

- 项目文档索引：[docs-deepshark/README.md](docs-deepshark/README.md)
- 仓库与目录维护：[docs-deepshark/repository.md](docs-deepshark/repository.md)
- 发布说明：[v2.1.0](docs-deepshark/releases/v2.1.0.zh-CN.md)、[v1.4.0](docs-deepshark/releases/v1.4.0.zh-CN.md)，全部版本见 [docs-deepshark/releases/](docs-deepshark/releases/)
- QGroundControl 官方用户文档：<https://docs.qgroundcontrol.com/>
- QGroundControl 官方开发文档：<https://dev.qgroundcontrol.com/>

## 基于 QGroundControl

[QGroundControl](https://github.com/mavlink/qgroundcontrol)（QGC）是面向 PX4、ArduPilot 等 MAVLink 载具的开源地面站，为本项目提供了遥测、参数管理、载具设置和视频流等基础。本仓库是定制发行版，不是 QGroundControl 官方发布；官方程序和下载请见[上游项目](https://github.com/mavlink/qgroundcontrol)。

## 源码来源

开发源码基于完整的官方 QGroundControl v5.1.5 工程，DeepShark 功能和必要的核心修复已适配到该版本。1.5.0 及更早的版本仍基于 v5.0.8。上游 QGroundControl 的提交历史完整保留，以便追溯并符合许可要求。

上游项目：

- <https://github.com/mavlink/qgroundcontrol>

## 许可

QGroundControl 按本仓库所附许可文件授权（[.github/COPYING.md](.github/COPYING.md)、[LICENSE-APACHE](LICENSE-APACHE)、[LICENSE-GPL](LICENSE-GPL)）。QGC_KevinJiang 保留上游的许可和署名。许可条款以上述原文为准。
