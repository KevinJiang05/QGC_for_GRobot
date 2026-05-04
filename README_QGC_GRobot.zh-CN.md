# QGC_GRobot / DeepShark Ground Station

本仓库是基于 **QGroundControl v5.0.8 Stable** 的水下机器人比赛地面站二次开发工程。当前策略是“方案 A”：保留 QGC 原生 ArduPilot / MAVLink / 参数 / 模式 / 遥测 / 控制能力，通过 QGC custom build 机制叠加 DeepShark 比赛专用界面。

第一版重点是可靠操控和多路低延迟 RTSP 观察，不重构 QGC 原生 `VideoManager`，不修改 MAVLink 协议定义，不替换 ArduPilot/APM firmware plugin。

## 当前能力

- QGC custom build 已接入，启动后加载 `DeepSharkPlugin`。
- Fly View 中覆盖 `FlyViewCustomLayer.qml`，显示 DeepShark 主视频面板。
- 支持四路 RTSP 视频通道。
- 支持自定义四路通道名称和 RTSP URL。
- 支持四宫格、一主三辅、单路面板内全屏、最小化 DeepShark Panel。
- Video Main Mode 下隐藏并禁用底层地图，以减少地图渲染和交互干扰。
- 保留 QGC 顶部连接状态栏、底部/右下角关键飞行仪表区域。
- 每路视频独立 Start / Stop / Reconnect。
- 支持 Reconnect All，并按顺序重启，避免四路同一瞬间抢资源。
- 每路独立状态机：`Waiting`、`Connecting`、`Streaming`、`Playing`、`Stopped`、`Failed`、`Reconnecting`、`Stalled`。
- 支持自动重连和 watchdog 冻结检测。
- 显示分辨率、FPS、watchdog 状态、估算接收端延迟。
- 右侧 DeepShark Status Panel 显示系统状态、视频状态、RTSP URL、Recent Events。

## 项目架构

DeepShark 改造尽量集中在 `custom/` 目录。

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

### Custom build 入口

`custom/CMakeLists.txt` 设置：

- `QGC_CUSTOM_BUILD`
- `CUSTOMHEADER="DeepSharkPlugin.h"`
- `CUSTOMCLASS=DeepSharkPlugin`
- 注册 `custom/custom.qrc`
- 注册 DeepShark C++ 源文件
- 将 `custom/src` 和 QGC `VideoReceiver` include path 加入构建

`custom/cmake/CustomOverrides.cmake` 保持最小化。当前不禁用 APM，不禁用 MAVLink，不修改 firmware plugin factory。

### QML 覆盖机制

`custom/custom.qrc` 使用 `/Custom/qml` 资源前缀覆盖 Fly View custom layer：

```xml
<file alias="QGroundControl/FlightDisplay/FlyViewCustomLayer.qml">
    src/FlyViewCustomLayer.qml
</file>
```

`DeepSharkPlugin` 使用 custom override/interceptor，让 QGC 加载 custom 资源中的 Fly View custom layer。这样可以插入 DeepShark UI，而不直接修改 `src/FlightDisplay/FlyView.qml`。

### Fly View custom layer

`custom/src/FlyViewCustomLayer.qml` 是 DeepShark UI 的顶层入口，负责：

- 显示/隐藏 DeepShark Video Panel。
- 显示/隐藏 DeepShark Status Panel。
- 控制 Video Main Mode。
- 在主视频模式下隐藏并禁用 `mapControl`。
- 维护 Recent Events。
- 保持 QGC `parentToolInsets` / `totalToolInsets` 透传。

### 视频面板

`custom/src/DeepShark/FourVideoPanel.qml` 负责四路视频的整体布局和交互：

- `grid`：2x2 四宫格。
- `mainAux`：一主三辅。
- `fullscreen`：单路面板内全屏。
- `minimized`：最小化 DeepShark Panel，恢复底层 QGC 地图。

布局切换不销毁 `VideoTile` 实例，尽量避免 RTSP 重连。

### 单路视频 Tile

`custom/src/DeepShark/VideoTile.qml` 负责单路视频显示、状态机和恢复逻辑：

- 绑定一个独立 `DeepSharkVideoController`。
- 独立 Start / Stop / Reconnect。
- 独立 retry 计数、lastError、watchdog 状态。
- 显示分辨率、FPS、估算延迟、状态、watchdog age。

### 视频控制器

`custom/src/DeepSharkVideoController.h/.cc` 是 DeepShark 自定义视频接入层。

它通过 QGC core plugin 创建原生 `VideoReceiver` 和 video sink：

- `QGCCorePlugin::createVideoReceiver`
- `QGCCorePlugin::createVideoSink`
- `QGCCorePlugin::releaseVideoSink`

这意味着 DeepShark 复用 QGC 现有 GStreamer / VideoReceiver 能力，但不改 `src/VideoManager/*`。

当前 controller 还在 GStreamer sink pad 上安装轻量 probe，用于统计：

- frame count
- FPS
- 分辨率
- 接收端估算延迟

延迟是基于 sink buffer PTS 和 pipeline clock 估算的接收端链路延迟，不等同于摄像头到屏幕的真实端到端物理延迟。

### 视频设置

`custom/src/DeepSharkVideoSettings.h/.cc` 使用 `QSettings` 保存四路通道名称和 RTSP URL。

设置面板保存后：

- 更新通道名称。
- 更新 RTSP URL。
- 名称同步到 Video Panel 和 Status Panel。
- URL 为空时对应通道进入 `Waiting`。
- URL 发生变化时只重启对应通道，不强制重启全部视频。
- 保存未变化配置不应触发不必要重连。

## Windows 开发环境

当前项目按 Windows 目标平台开发。

已验证的本机路径：

```text
Qt:         D:\Develop\Qt\6.8.3\msvc2022_64
Visual Studio: D:\Develop\Vs2022\Community
GStreamer: D:\gstreamer\1.0\msvc_x86_64
Build dir: build
```

如果迁移到其他电脑，路径可以不同，但需要保持：

- Qt 6.8.x MSVC 64-bit
- Visual Studio 2022 C++ toolchain
- CMake
- Ninja
- GStreamer MSVC x86_64 runtime/development package

## 构建

在仓库根目录执行：

```powershell
cmd.exe /c "call D:\Develop\Vs2022\Community\VC\Auxiliary\Build\vcvars64.bat >nul && cmake --build build --config Debug"
```

如果链接时报错：

```text
LINK : fatal error LNK1168: cannot open Debug\QGroundControl.exe for writing
```

说明 QGC 正在运行，占用了 exe。先关闭 QGC：

```powershell
Get-Process QGroundControl -ErrorAction SilentlyContinue | Stop-Process
```

然后重新 build。

## 运行

不要直接双击 `build\Debug\QGroundControl.exe` 来运行视频版本。RTSP/GStreamer 需要环境变量，推荐使用：

```powershell
D:\Develop\QGC_for_GRobot\StartDeepSharkQGC.cmd
```

该脚本会设置：

- `PATH`
- `GST_PLUGIN_PATH`
- `GST_PLUGIN_SYSTEM_PATH`
- `GST_PLUGIN_SCANNER`
- `GIO_EXTRA_MODULES`

然后启动 Debug 版 QGC。

## RTSP 使用

打开 QGC 后进入 Fly View，DeepShark Video Panel 默认显示。

常用操作：

- `设置`：编辑四路视频名称和 RTSP URL。
- `启动`：启动当前/默认视频通道。
- `停止`：手动停止视频。手动停止后不会自动重连。
- `重连`：重连当前选中通道。
- `全部重连`：按顺序重连四路视频。
- `四宫格`：回到 2x2 总览布局。
- `主辅`：进入一主三辅布局。
- `全屏`：将选中通道放大到 DeepShark 面板内。
- `最小化`：隐藏 DeepShark Panel，恢复 QGC 原生地图视图。
- `显示地图`：切换地图显示/隐藏。

建议先用 VLC 或 PotPlayer 验证 RTSP 源：

```text
rtsp://<ip>:<port>/<path>
```

确认外部播放器能播放后，再接入 DeepShark。

## 稳定性机制

### 独立通道

四路视频互相独立。一路 URL 错误、断流或重连，不应导致其他三路停止。

### 自动重连

非手动停止导致的失败会触发自动重连：

- 第一次失败后约 3 秒重连。
- 后续约 5 秒重连。
- 连续失败超过上限后进入 `Failed`，等待人工点击 Reconnect。

### 手动停止

用户点击 Stop 后：

- 状态变为 `Stopped`。
- 自动重连禁用。
- watchdog 不会触发重连。
- 需要手动 Start/Reconnect 才恢复。

### Watchdog

每路视频使用 sink frame count 判断是否有真实帧进展。

如果处于播放状态但约 8 秒没有新帧：

- 标记为 `Stalled`。
- 记录 Recent Event。
- 触发受控重连。

watchdog 重连有节流，避免无限高速重启。

### 延迟显示

每路显示 `Latency: xx ms` 或 `Latency: --`。

该值是接收端估算延迟：

- 基于 GStreamer buffer PTS 和 pipeline clock。
- 用于判断接收端是否堆帧或滞后。
- 不等于摄像头真实端到端延迟。
- 如果 RTSP 源没有有效 PTS，会显示 `--`。

## 状态面板

DeepShark Status Panel 显示：

- 当前布局模式。
- 当前主视图。
- 地图状态。
- DeepShark Panel 状态。
- Vehicle 状态占位。
- 每路视频状态、retry、watchdog、FPS、延迟、lastError。
- 四路 RTSP URL。
- 最近事件。

Recent Events 支持滚动，只记录 DeepShark custom 层能捕获的事件，不读取 QGC 全局日志文件。

## 开发边界

当前项目原则：

- 优先新增/修改 `custom/`。
- 不改 `src/FlightDisplay/FlyView.qml`。
- 不改 `src/FlightDisplay/FlyViewWidgetLayer.qml`。
- 不改 `src/VideoManager/*`。
- 不改 `src/MAVLink/*`。
- 不改 `src/FirmwarePlugin/APM/*`。
- 不改 `src/Vehicle/*`。
- 不改 `src/Comms/*`。
- 不修改 ArduPilot/APM 插件配置。
- 不修改 MAVLink 协议定义。
- 不重构 QGC 原生视频链路。

如后续必须触碰 QGC 原生模块，应先写清楚原因、影响范围、回滚方案，再小步实施。

## 建议开发流程

1. 修改前先看 `git status --short`，确认工作区状态。
2. 每次只解决一个问题。
3. 优先改 `custom/`。
4. 改完先跑定向构建。
5. 能启动的改动要运行 `StartDeepSharkQGC.cmd` 做冒烟测试。
6. 视频相关改动至少用一条 RTSP 源验证。
7. 多路稳定性改动再验证两路/四路。
8. 每个阶段单独提交，提交信息说明功能边界。

## 常见问题

### 双击 exe 没反应或找不到 GStreamer 插件

使用 `StartDeepSharkQGC.cmd` 启动，不要直接双击 exe。

### 构建时 exe 无法写入

QGC 正在运行。关闭进程后重新构建：

```powershell
Get-Process QGroundControl -ErrorAction SilentlyContinue | Stop-Process
```

### VLC 能播，DeepShark 不能播

优先检查：

- 是否通过 `StartDeepSharkQGC.cmd` 启动。
- RTSP URL 是否保存到对应通道。
- GStreamer MSI 是否安装在脚本指定路径。
- 手机/摄像头和电脑是否在同一网段。
- 防火墙是否拦截。
- RTSP 源是否有音频轨、是否为 H.264/H.265、是否支持多客户端。

### 延迟显示为 `--`

说明当前流没有提供可用 PTS，或 pipeline 当前未 decoding。这不一定代表视频不可用。

## 当前限制

- 尚未接入真实机器人联调。
- Vehicle 状态面板目前仍是占位/基础显示，未深度绑定 active vehicle。
- 未做视频录制。
- 未做云台控制。
- 未做机械臂控制。
- 未做 3D 姿态窗口。
- 延迟为接收端估算值，不是真实端到端延迟。
- Status Panel 不读取 QGC 全局日志。

## 后续路线建议

优先级建议：

1. 实验室真实连接 ArduPilot / MAVLink，验证遥测、模式、解锁、安全控制链路。
2. 真实机器人四路 RTSP 长时间稳定性测试。
3. 根据实测调整 RTSP latency、watchdog 阈值、重连上限。
4. 增加轻量 3D 姿态窗，绑定 active vehicle roll / pitch / yaw。
5. 比赛 UI 固化：减少误触、固定布局、快捷恢复。
6. 根据比赛需求再考虑日志导出、录制、相机控制等功能。

