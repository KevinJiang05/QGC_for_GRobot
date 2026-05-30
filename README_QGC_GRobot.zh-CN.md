# QGC_GRobot / DeepShark 地面站

本仓库是在 **QGroundControl v5.0.8 Stable** 基础上，为水下机器人比赛和后续 AUV/ROV 实验开发的定制地面站。当前策略是保留 QGC 原生 ArduPilot、MAVLink、参数、模式、遥测和控制链路，通过 QGC custom build 机制叠加 DeepShark 专用视频面板与 YOLO 目标检测叠加层。

核心原则是小步改造：优先复用 QGC 原有视频、GStreamer、MAVLink 和设置体系，不重构无关架构，不替换原生飞控插件。

## 当前能力

- DeepShark custom build 已接入，启动后加载 `DeepSharkPlugin`。
- Fly View 中加载 DeepShark 专用四路视频面板。
- 支持四路 RTSP 视频源，通道名称和 URL 可在面板设置中保存。
- 支持四宫格、主辅布局、单路面板内全屏、最小化和地图显示切换。
- 每路视频独立 Start、Stop、Reconnect，并有独立状态、重连计数和 watchdog。
- 支持按顺序全部重连，避免四路视频同时抢占资源。
- 支持 YOLO 检测框叠加到 QGC 原生视频和 DeepShark 四路 Video Panel。
- 支持按视频源路由检测框：`deepSharkVideo1` 到 `deepSharkVideo4` 分别对应四个 tile。
- 支持在 QGC 视频设置和 DeepShark 设置面板中开关 `YOLO Detection Overlay`。
- YOLO 桥接程序可自动读取 QGC 已保存的四路 RTSP URL，并为每路启动一个检测子进程。

## 目录结构

DeepShark 定制代码尽量集中在 `custom/`，AI 桥接工具集中在 `tools/ai_detection/`。

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

src/FlightDisplay/
  AIDetectionReceiver.h/.cc
  AIDetectionVideoOverlay.qml

tools/ai_detection/
  yolo_to_qgc_udp.py
  run_yolo_to_qgc_auto.py
  run_yolo_to_qgc_udp.bat
  send_sample_detection.py
  requirements.txt
```

## 启动脚本

根目录提供两个常用启动脚本：

```powershell
.\StartDeepSharkQGC.cmd
.\StartYoloToQGC.cmd
```

`StartDeepSharkQGC.cmd` 负责设置 Qt、GStreamer、MSVC 运行库相关环境变量，然后启动：

```text
build-debug-ai\Debug\QGroundControl.exe
```

不要直接双击 exe 启动视频版本。RTSP/GStreamer 依赖启动脚本中的环境变量。

`StartYoloToQGC.cmd` 负责使用共享 YOLO 环境启动桥接程序。无参数运行时，它会自动读取 QGC 保存的 DeepShark 四路 RTSP URL。

## Windows 开发环境

当前工作区使用的本机路径如下：

```text
项目目录:       D:\Develop\QGC_for_GRobot
构建目录:       D:\Develop\QGC_for_GRobot\build-debug-ai
Qt:             D:\Develop\Toolchains\Qt\6.8.3\msvc2022_64
GStreamer:      D:\Develop\Toolchains\GStreamer\1.0\msvc_x86_64
VS Build Tools: D:\Develop\Toolchains\VS2022BuildTools
YOLO 共享环境:  D:\Develop\envs\yolo
```

如迁移到其他电脑，路径可以不同，但需要保持：

- Qt 6.8.x MSVC 64-bit
- Visual Studio 2022 C++ Build Tools
- CMake
- Ninja
- GStreamer MSVC x86_64 runtime/development package
- Python venv 中安装 `ultralytics`

## 构建

在仓库根目录执行：

```powershell
cmd /s /c ""D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build build-debug-ai --target QGroundControl"
```

如果链接时报错：

```text
LINK : fatal error LNK1168: cannot open Debug\QGroundControl.exe for writing
```

说明 QGC 仍在运行，占用了 exe。先关闭 QGC，或执行：

```powershell
Get-Process QGroundControl -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "*QGC_for_GRobot*build-debug-ai*" } |
    Stop-Process
```

然后重新构建。

## RTSP 使用流程

1. 手机或相机开启 RTSP 推流。
2. 先用 VLC 或 PotPlayer 验证 URL 是否能播放。
3. 运行 `StartDeepSharkQGC.cmd`。
4. 进入 Fly View，打开 DeepShark Video Panel。
5. 点击 `设置`，填入四路视频名称和 RTSP URL。
6. 点击 `启动` 或 `全部重连`。

常见 RTSP 格式：

```text
rtsp://192.168.2.189:8552/live
rtsp://user:password@192.168.2.189:8554/stream1
```

四路通道在内部对应：

```text
video1 -> deepSharkVideo1
video2 -> deepSharkVideo2
下视   -> deepSharkVideo3
备用/侧视 -> deepSharkVideo4
```

## YOLO 检测叠加

QGC 内部监听 UDP `127.0.0.1:57610`，接收 JSON 检测结果并绘制检测框。YOLO 推理由外部 Python 桥接程序执行，QGC 本体不直接加载 PyTorch。

推荐启动顺序：

1. 运行 `StartDeepSharkQGC.cmd`。
2. 在 DeepShark 设置中保存四路 RTSP URL。
3. 确认需要识别的视频能正常播放。
4. 运行 `StartYoloToQGC.cmd`。

无参数运行 `StartYoloToQGC.cmd` 时，它会读取 QGC 设置文件中的四路 URL，并自动启动四个子进程：

```text
deepSharkVideo1 -> 第一路 URL
deepSharkVideo2 -> 第二路 URL
deepSharkVideo3 -> 第三路 URL
deepSharkVideo4 -> 第四路 URL
```

只跑单路源时，也可以手动指定：

```powershell
.\StartYoloToQGC.cmd "rtsp://192.168.2.189:8552/live"
```

查看自动读取到的源但不启动 YOLO：

```powershell
.\StartYoloToQGC.cmd --dry-run
```

## YOLO 共享环境

YOLO 依赖安装在共享虚拟环境：

```text
D:\Develop\envs\yolo
```

创建和安装依赖：

```powershell
python -m venv D:\Develop\envs\yolo
D:\Develop\envs\yolo\Scripts\python.exe -m pip install --upgrade pip
D:\Develop\envs\yolo\Scripts\python.exe -m pip install -r tools\ai_detection\requirements.txt
```

默认模型路径：

```text
D:\Develop\envs\yolo\models\yolov8n.pt
```

如果后续训练了水下目标模型，建议把权重放在共享环境或项目外统一模型目录，然后修改 `tools\ai_detection\run_yolo_to_qgc_udp.bat` 中的 `DEFAULT_MODEL`。

## 检测数据格式

QGC 支持归一化框：

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

也支持像素坐标框：

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

`source_id` 用于把检测框路由到对应视频 tile。没有 `source_id` 的检测结果只会显示在默认 QGC 原生视频叠加层。

## YOLO 叠加开关

开关位置：

- QGC: `Application Settings -> Video -> YOLO Detection Overlay`
- DeepShark: `DeepShark Video Panel -> 设置 -> YOLO Detection Overlay`

关闭后，QGC 仍可接收 UDP 检测数据，但界面不绘制检测框。

## 稳定性设计

- 四路视频互相独立，一路断流不应影响其他三路。
- 手动 Stop 后不会自动重连，需要手动 Start 或 Reconnect。
- 非手动停止导致的异常会触发自动重连。
- Watchdog 会检测长时间无帧进展的通道，并触发受控重连。
- 退出时会停止 DeepShark tile 的重连定时器、watchdog 和视频控制器，避免关闭阶段再次启动视频。

当前为了优先保证退出稳定性，DeepShark 视频 tile 的 FPS 和 Latency 可能显示为 `--`。这不代表视频不可用，也不影响 YOLO 绘制。

## 常见问题

### 双击 exe 后视频不可用

使用根目录脚本启动：

```powershell
.\StartDeepSharkQGC.cmd
```

### YOLO 桥接窗口提示找不到 Python

检查共享环境是否存在：

```powershell
D:\Develop\envs\yolo\Scripts\python.exe --version
```

如果不存在，重新创建共享环境并安装 `tools\ai_detection\requirements.txt`。

### QGC 中有视频但没有检测框

按顺序检查：

- `YOLO Detection Overlay` 是否开启。
- `StartYoloToQGC.cmd` 是否正在运行。
- 桥接窗口是否持续输出 `sent ... detections to 127.0.0.1:57610`。
- DeepShark 设置里保存的 RTSP URL 是否和正在播放的通道一致。
- 自动桥接时 `source_id` 是否对应当前 tile。

### 检测框偏移或出现在黑边

当前 overlay 会根据视频真实宽高计算内容区域，只在有效视频画面内绘制。若仍有偏移，优先确认桥接发送的 `frame_width`、`frame_height` 是否与实际推理帧一致。

### 关闭 QGC 时 Debug CRT 报 heap corruption

这不是 YOLO 桥接未关闭导致的。桥接是外部 UDP 发送进程，QGC 关闭后 UDP 包会被系统丢弃。若仍复现，应继续排查 QGC 内部视频对象、GStreamer sink 或异步重连回调的释放顺序。

## 开发边界

当前项目原则：

- 优先修改 `custom/` 和 `tools/ai_detection/`。
- 尽量不改 `src/VideoManager/*`。
- 尽量不改 MAVLink、FirmwarePlugin、Vehicle、Comms 相关核心链路。
- QGC 原生模块若必须修改，应说明原因、影响范围和回滚方式。
- 不为了单个实验功能大范围重构 QGC 架构。

## 建议开发流程

1. 修改前先看 `git status --short`。
2. 每次只解决一个明确问题。
3. 视频 UI 优先改 `custom/src/DeepShark/`。
4. AI 桥接优先改 `tools/ai_detection/`。
5. 改完先跑定向构建。
6. 能启动的改动用 `StartDeepSharkQGC.cmd` 做冒烟测试。
7. YOLO 改动先用 `send_sample_detection.py` 验证 QGC 绘制，再跑真实 YOLO。
8. 多路视频改动至少测一路真实 RTSP，稳定性改动再测两路或四路。

## 后续路线建议

1. 接入真实 ArduPilot / MAVLink 机器人，验证遥测、模式、解锁和安全控制链路。
2. 用真实四路 RTSP 长时间测试 DeepShark Video Panel。
3. 根据实测调 RTSP latency、watchdog 阈值和重连上限。
4. 训练或微调水下目标专用模型，替换默认 COCO `yolov8n.pt`。
5. 增加轻量 3D 姿态窗口，绑定 active vehicle 的 roll/pitch/yaw。
6. 在核心链路稳定后，再考虑录像、日志导出、云台或机械臂控制。
