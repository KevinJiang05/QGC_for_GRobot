# QGC_GRobot / DeepShark 地面站

本仓库是在 **QGroundControl v5.0.8 Stable** 基础上，为水下机器人比赛和后续 AUV/ROV 实验开发的定制地面站。当前内部 Windows 发行版本为 **QGC_KevinJiang v1.3.1**。项目保留 QGC 原生 ArduPilot、MAVLink、参数、模式、遥测和控制链路，通过 QGC custom build 机制叠加 DeepShark 专用视频面板、YOLO 目标检测叠加层和设备配置工具。

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
- 已加入 ArduSub 4.8 参数元数据和固件原生手柄动作，支持在 UI 中配置 `actuator_4_inc/dec`。
- Servo12 机械爪可通过 Actuator4 实现“按住运动、松手保持当前位置”。
- 推进器测试、物理输出 PWM 直发和舵机点动是三条独立路径，便于区分推进器与舵机调试需求。
- “视频解码优先级”已显式显示，可在默认、软件和硬件解码策略间切换。

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

## 启动脚本

根目录提供三个开发/现场辅助脚本：

```powershell
.\StartDeepSharkQGC.cmd
.\StartYoloToQGC.cmd
.\StopDeepSharkQGC.cmd
```

`StartDeepSharkQGC.cmd` 是**本机开发环境**入口：它负责设置 Qt、GStreamer、MSVC 运行库相关环境变量，然后优先启动：

```text
build-debug-ai\Debug\QGC_KevinJiang.exe
```

Debug 版不要直接双击 exe 启动视频版本，RTSP/GStreamer 依赖启动脚本中的环境变量。正式安装版已携带运行依赖，应从 Windows 开始菜单或安装目录启动，不应依赖本机 `D:\Develop` 工具链。

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
cmd /s /c ""D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build build-debug-ai --target QGC_KevinJiang"
```

如果链接时报错：

```text
LINK : fatal error LNK1168: cannot open Debug\QGC_KevinJiang.exe for writing
```

说明 QGC 仍在运行，占用了 exe。先关闭 QGC，或执行：

```powershell
Get-Process QGC_KevinJiang -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "*QGC_for_GRobot*build-debug-ai*" } |
    Stop-Process
```

然后重新构建。

## Windows 发布

发布 Windows 安装包时，优先使用固化脚本：

```powershell
.\tools\release\build-windows-release.ps1 -Version 1.3.2
```

脚本会产生逐步日志、`release-state.json` 和 `release-report.md`。只有最终报告成功且版本、依赖、哈希和覆盖升级契约通过时，才建议分发。完整流程见 `docs/releases/windows-release-runbook.md`；当前 v1.3.1 发布说明见 `docs/releases/v1.3.1.zh-CN.md`。

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

## ArduSub 4.8、机械爪与物理输出测试

本项目定制 ArduSub 4.8 固件可使用固件原生按钮函数。Servo12 机械爪已验证的连续控制方案是：`SERVO12_FUNCTION=187`（Actuator4）、`BTN9_FUNCTION=116`（`actuator_4_dec`）、`BTN10_FUNCTION=115`（`actuator_4_inc`）。松开按键后机械爪保持当前位置；实际 PWM 行程必须根据每台机器的机械限位确认。

完整参数、方向和速度调整说明见 `docs/research/problem/Servo12机械爪手柄配置说明.md`。首次测试前必须确认飞控上锁、机器人固定、推进器和机械爪周边无人。

“SERVO 输出扫描向导”中三种测试方式不可混用：

- `Motor Test`：飞控原生推进器测试。
- `推进器直发PWM`：按物理输出口发 PWM，不走 `SERVOx_FUNCTION` 映射，结束回到 1500 PWM。
- `舵机点动`：按物理输出口发 PWM，结束回到该通道 `SERVOx_TRIM`。

## 飞控 TCP 连接诊断

标准串口服务器配置为 `TCP Server / 192.168.1.200:4019 / 57600 / 8N1`。QGC 建立 TCP 会话只代表网络端口可达；只有持续收到 MAVLink HEARTBEAT 后，QGC 才会识别载具。

无侵入检查：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnostics\check-grobot-link.ps1
```

若 TCP 已建立但没有载具，应优先检查飞控供电、串口 TX/RX 交叉和共地、实际 `SERIALx_PROTOCOL=2`、`SERIALx_BAUD=57`，而不是反复修改 QGC 连接配置。详细复盘见 `docs/audits/flight_controller_connection_incident_2026-07-21.md`。

## 稳定性设计

- 四路视频互相独立，一路断流不应影响其他三路。
- 手动 Stop 后不会自动重连，需要手动 Start 或 Reconnect。
- 非手动停止导致的异常会触发自动重连。
- Watchdog 会检测长时间无帧进展的通道，并触发受控重连。
- 退出时会停止 DeepShark tile 的重连定时器、watchdog 和视频控制器，避免关闭阶段再次启动视频。

当前为了优先保证退出稳定性，DeepShark 视频 tile 的 FPS 和 Latency 可能显示为 `--`。这不代表视频不可用，也不影响 YOLO 绘制。多路 RTSP 在不同显卡、驱动和解码策略下仍可能偶发少一路；可先使用“全部重连”，或在“应用设置 → 视频”调整“视频解码优先级”。

## 常见问题

### 双击 exe 后视频不可用

使用根目录脚本启动：

```powershell
.\StartDeepSharkQGC.cmd
```

如果是正式安装版，先确认没有混用 Debug 目录中的 DLL；正式安装版应直接通过开始菜单或安装目录启动。

### 启动后有一路 RTSP 未播放或 Debug 版出现 Qt QML 断言

先点击“全部重连”，并记录卡住通道、视频解码优先级、显卡型号和驱动版本。可先切换为软件解码验证兼容性。当前措施是降低复现概率和提供回退设置，尚不能视为根因完全消除。

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

1. 继续排查更换机器人后飞控未上电、无 MAVLink 字节流的问题，先恢复硬件供电再检查串口参数和接线。
2. 用真实四路 RTSP 长时间测试 DeepShark Video Panel，并收集 Qt/GStreamer 兼容性证据。
3. 使用电源侧仪表排查推进器高输出区间速度偏慢的问题；当前飞控电源遥测不可信。
4. 根据实测调 RTSP latency、watchdog 阈值和重连上限。
5. 训练或微调水下目标专用模型，替换默认 COCO `yolov8n.pt`。
6. 在核心链路稳定后，再考虑录像、日志导出、云台或机械臂控制。
