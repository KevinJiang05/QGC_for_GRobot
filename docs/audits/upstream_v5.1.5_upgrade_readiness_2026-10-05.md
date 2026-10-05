# QGC for GRobot 升级官方 v5.1.5 的准备度审计

> 审计日期：2026-10-05；原审计窗口：18:08–18:20，Asia/Taipei。
> 本文固化本次只读审计的结论，不表示已经实施升级，也不表示新版本已经通过编译、设备或安装验收。

## 1. 结论与建议

官方 QGroundControl v5.1.5 可以作为后续升级目标，但不能当作一次只改版本号的更新。当前定制涉及插件退出顺序、QML 模块与资源覆盖、四路视频显示后端、接收器生命周期、手柄配置和安装升级契约。新版对其中一部分问题已有修复，另一部分需要重新适配，仍有若干问题不能直接删除当前补丁。

建议先完成当前稳定版的发布与验证，再从明确的发布基线开展升级。原讨论中距离比赛约九天，比赛主版本应继续使用已有验证证据的版本；只有新版完成离线回归、实际设备验证和安装配置回归后，才考虑替换比赛版本。编译成功不能替代比赛验收，也不能据此承诺完成日期。

优先处理的阻断点是：

1. 新版先销毁 QML 引擎再执行插件清理；当前插件持有原始引擎指针，存在退出时访问失效对象的风险。
2. 新视频后端需要显式连接显示 sink；当前四路视频控制器的显示连接方式不能原样迁移。
3. 新版手柄配置结构变化，未找到旧配置的完整自动迁移路径。校准、按钮和启用状态不能仅靠复制配置或保留同一 settings version 保证兼容。
4. 接收器自动重连与当前控制器、QML 的重试逻辑需要确定唯一所有者，避免重复重连或手动停止后重新启动。
5. 当前发布脚本、依赖路径和安装器应按新版实际结构适配，并验证旧配置保留和回退。

## 2. 审计边界与版本身份

### 2.1 原审计快照

| 项目 | 原审计观察 |
| --- | --- |
| 仓库 | `D:\Develop\QGC_for_GRobot` |
| 分支 | `deepshark/v5.0.8` |
| HEAD | `061e839009dbbd259a050ec3b7eeb708de96ef00` |
| HEAD 提交标题 | `Drop AI overlay hooks from QGC native video views` |
| 原审计开始与结束时的已跟踪脏文件 | `custom/GRobot.rc`、`custom/cmake/CustomOverrides.cmake` |
| 两个文件中的发布版本变更 | `1.4.0` → `1.5.0` |
| 相对官方 v5.0.8 的既有文件修改 | 65 个路径，其中 `src/` 下 44 个 |
| 新增且已跟踪的 `custom/` 文件 | 35 个 |

原审计窗口内 HEAD 未变化。多次读取并非原子快照，被忽略的构建、打包目录及日志可能同时变化，因此本文不能用来证明并行发布任务的最终状态。

### 2.2 官方标签与发布

| 标签 | Annotated tag 对象 | 实际提交 |
| --- | --- | --- |
| v5.0.8 | `b05e82c3e41157396c9d90e91500161f066987cd` | `e0816c957602789200ae5ba0af45217f0f2f1db4` |
| v5.1.5 | `9428722253158ae7600e45a72f2951c8e60b88b1` | `3a67d31f0c36bf3fe38ec52970d250a89d0aaf67` |

本地已有 Git 对象与原审计期间查询的公开 GitHub ref/tag 信息一致。比较代码使用实际提交，不能把 annotated tag 对象 SHA 当成源码提交 SHA。

v5.1.5 官方发布在原审计查询时不是 draft，也不是 prerelease；发布时间为 `2026-10-01T17:43:23Z`，即台北时间 2026-10-02 01:43:23。来源：[官方 v5.1.5 Release](https://github.com/mavlink/qgroundcontrol/releases/tag/v5.1.5)。

### 2.3 执行范围

原审计读取了当前源码、已有 Git 标签对象、项目构建与发布配置，并查询官方发布和源码信息。没有执行 fetch、clone、迁移、构建、测试、应用启动、安装、依赖准备或设备操作，没有调用 Claude。

没有读取或输出凭据、`.env` 敏感值，也没有读取实际固件文件或个人日志内容。历史审计只用于定位当前补丁，结论以原审计时的本地源码和官方标签源码为准。

现有 [2026-09-02 上游补丁清单](upstream_patch_inventory_2026-09-02.md) 的顶部统计已更新到 65 个既有路径、44 个 `src/` 路径，但逐模块内容仍包含旧的 53 个路径基线、旧日期和未跟踪测试描述。它可以提供背景，不能直接代替本次完整清单。

## 3. 新增定制模块：保留意图，按新版接口适配

35 个已跟踪 `custom/` 文件都有明确的当前功能用途。升级时应保留这些功能意图，调整资源路径、初始化和生命周期接口；不能因新版目录调整就整体删除。

| 功能 | 文件 | 数量 |
| --- | --- | ---: |
| 插件、界面入口与构建品牌 | `custom/src/DeepSharkPlugin.cc`、`.h`；`custom/src/FlyViewCustomLayer.qml`；`custom/CMakeLists.txt`；`custom/custom.qrc`；`custom/cmake/CustomOverrides.cmake`；`custom/GRobot.rc` | 7 |
| AI 检测 | `custom/src/AIDetectionManager.cc`、`.h`；`custom/src/AIDetectionReceiver.cc`、`.h`；`custom/src/DeepShark/AIDetectionSettings.qml`、`AIDetectionVideoOverlay.qml` | 6 |
| 四路视频 | `custom/src/DeepSharkVideoController.cc`、`.h`；`custom/src/DeepSharkVideoSettings.cc`、`.h`；`custom/src/DeepShark/FourVideoPanel.qml`、`VideoTile.qml` | 6 |
| AUV 任务与姿态状态 | `custom/src/DeepSharkAuvController.cc`、`.h`；`custom/src/DeepShark/AuvMissionPanel.qml`、`Attitude3DPanel.qml`、`DeepSharkStatusPanel.qml` | 5 |
| 连接监控与提示音 | `custom/src/DeepSharkConnectionMonitor.cc`、`.h`；`custom/src/DeepShark/ConnectionAlertBanner.qml`、`ConnectionAlertSettings.qml`；`custom/audio/connection-alert.wav`、`connection-alert-hi-low.wav` | 6 |
| 推进器控制和映射工具 | `custom/src/ThrusterDirectControlController.cc`、`.h`；`custom/src/ThrusterMappingExportController.cc`、`.h`；`custom/src/DeepShark/ThrusterMappingTool.qml` | 5 |
| 合计 | | 35 |

另有 `test/DeepShark` 下 11 个文件、5 个测试类，以及 `tools/ai_detection`、`tools/rtsp`、品牌资源和启动脚本。它们应按实际用途迁移。根目录 `StartDeepSharkQGC.cmd`、`StartYoloToQGC.cmd`、`StopDeepSharkQGC.cmd` 及发布脚本中的旧依赖路径需要检查，不能直接假定适用于新版环境。

## 4. 全部 65 个既有文件补丁的分类

这里的“退休”是升级实施阶段的候选处理，不是本次审计执行了删除。“不确定”表示必须通过对应版本的实际验证才能判断。多个行为集中在同一文件时，应逐行为处理，不能按文件整体覆盖。

### 4.1 Qt 包装对象与垃圾回收：6 个，不确定

- `src/ADSB/ADSBVehicleManager.h`
- `src/API/QGCCorePlugin.cc`
- `src/API/QGCCorePlugin.h`
- `src/API/QGCOptions.h`
- `src/AnalyzeView/MAVLinkMessageField.h`
- `src/Joystick/Joystick.h`

当前补丁包含对 const 属性和 Qt 包装对象生命周期的处理。新版仍有相关 const 属性，`MAVLinkMessageField` 则迁往 `AnalyzeView/MAVLinkInspector/`。应在 Qt 6.11 上验证引擎创建、销毁及强制垃圾回收，再决定能否退休；不能只因 Qt 版本升级就认定根因已消失。

### 4.2 原生视频禁用行为：2 个，保留意图并适配

- `src/Vehicle/Vehicle.cc`
- `src/VideoManager/VideoManager.cc`

新版仍会针对 ArduSub 将禁用状态改为 UDP，并存在自动视频流覆盖逻辑。当前定制需要在四路视频工作时保持原生视频禁用。原生视频界面不再使用，并不表示这些状态处理可以删除。

### 4.3 GStreamer 接收器：3 个，适配与部分退休

- `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.cc`
- `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.h`
- `src/VideoManager/VideoReceiver/VideoReceiver.h`

新版已经覆盖一部分当前修复：`_lastSourceFrameTime`、`_lastVideoFrameTime`、`_decoding` 的原子状态处理，pad 到 pad 的连接，以及部分父对象引用释放。这些行为应按新版实现逐项复核，避免叠加重复修复。

仍需保留或重新适配的行为包括：

- `stop()` 在 URI 为空时仍提前返回，早于取消启动 epoch、释放管线和完成通知。当前清空 URI 后仍能停止的修复不能直接删除。
- 新版 `_streaming`、`_endOfStream` 仍为普通布尔值；需要核对当前跨线程访问与原子化要求。
- `_onNewDecoderPad` 未覆盖当前 caps 和重复 pad 防护；重复连接失败后移除 sink 的路径仍需回归。
- 新版尺寸读取来自 `_decoderValve` 的编码侧 caps 查询，不能等同于当前从解码后协商 caps 读取实际尺寸并释放引用的行为。还需结合新版方向元数据适配。
- 新 SourceFactory 已过滤部分视频 pad，但 RTP 分类范围较宽，`_onNewSourcePad` 中仍有过滤相关 FIXME。不能认定当前 `_isVideoPad` 已被完全覆盖。
- 当前 tsdemux 的低延迟与 `latency=0` 设置，在新版 SourceFactory 中未找到等价覆盖。

已经失去调用用途的 reset 或 signal-depth 清理代码不必机械迁移。接收器的引用释放、停机和并发修复，也不能用来证明现场偶发卡顿已经解决。

### 4.4 默认 TCP 链路与代理绕过：3 个，保留意图

- `src/Comms/LinkManager.cc`
- `src/Comms/LinkManager.h`
- `src/Comms/TCPLink.cc`

保留 `_addGRobotDefaultTCPLinkIfNeeded` 的默认连接和避免重复创建行为：名称 `GRobot_Default`、地址 `192.168.1.200`、端口 `4019`、自动连接，并保护已有同名、匹配或用户配置的链接。新版 `setupSocket` 未见当前 `NoProxy` 等价设置，代理绕过意图需要保留。默认连接创建可以放在合适的现有定制所有者中，不必原样保留补丁位置。

### 4.5 连接监控接入点：3 个，适配

- `src/Vehicle/Vehicle.h`
- `src/UI/AppSettings/LinkSettings.qml`
- `src/QmlControls/FlyViewToolBar.qml`

保留关闭 Vehicle 前发出的 `connectionCloseRequested`、移除或断开链接前的 `prepareLinkDisconnect`，以及连接告警横幅。新版工具栏和设置页面组织变化，需要重新确定接入点，避免主动断开被误报为故障。

### 4.6 MockLink 线程与晚到写入：3 个，部分覆盖，仍需验证

- `src/Comms/MockLink/MockLink.cc`
- `src/Comms/MockLink/MockLink.h`
- `src/Comms/MockLink/MockLinkWorker.cc`

当前实现将 worker 放在所有者线程，以精确 2 ms 定时器随链接连接和断开启动、停止，并防护晚到队列写入与无效通道。新版继续使用 worker QThread，但在断开时退出并等待线程，并丢弃未连接或无效通道的写入。

新版已覆盖部分防护，但不能据此认定当前全部问题已经解决。不要整体复制旧线程结构，也不要未经同线程和断开后晚到写入测试就删除全部当前处理。

### 4.7 相机晚到回调：1 个，适配

- `src/Camera/QGCCameraManager.cc`

当前回调上下文由 Vehicle 持有，并使用 `QPointer<CameraStruct>` 防护 manager 销毁后的晚到 ACK。新版使用含 `QPointer<manager>` 的 `CameraInfoRequestContext`，但上下文仍由 manager 持有，析构中删除上下文，晚到回调仍可能解引用该上下文。这里存在需要验证的悬空访问风险，不能将新版弱指针视为完全覆盖。保留 `_lateCameraReplyAfterManagerDestroyed` 的验证意图。

### 4.8 ArduSub 配置页面：6 个，适配与部分退休

- `src/AutoPilotPlugins/APM/APMCameraComponentSummary.qml`
- `src/AutoPilotPlugins/APM/APMCameraSubComponent.qml`
- `src/AutoPilotPlugins/APM/APMLightsComponent.qml`
- `src/AutoPilotPlugins/APM/APMSafetyComponentSub.qml`
- `src/AutoPilotPlugins/APM/APMSafetyComponentSummarySub.qml`
- `src/UI/toolbar/APMMainStatusIndicatorContentItem.qml`

旧 mount 页面被新的 `APMGimbalComponent`、Instance 和 Params 结构替代，使用 `MNT#` 参数。旧 mount 参数兼容需求应通过实际固件参数 fixture 判断。

新版安全页面支持 `ARMING_CHECK` 与 `ARMING_SKIPCHK`，通用兼容已有覆盖；当前默认 `0x00fffe3f` 的语义和告警仍需判断是否保留。新版 Lights 已将 `BRD_PWM_COUNT` 作为可选参数并提供输出范围回退，但与当前从实际 `SERVO#_FUNCTION` 推断通道的行为不等价，范围防护仍需适配。

新版 failsafe 通过 VehicleConfig JSON 组织。缺少可选 `FS_GCS_ENABLE`、`FS_GCS_TIMEOUT`、`FS_OPTIONS` 时的防护，应按新版页面和真实参数集复核。

### 4.9 参数元数据：3 个，退休旧实现，适配数据来源

- `src/FirmwarePlugin/APM/APMParameterMetaData.cc`
- `src/FirmwarePlugin/APM/APMResources.qrc`
- `src/FirmwarePlugin/APM/ArduPilot-Parameter-Repository`

新版移除了旧 qrc 和 gitlink，改为 CPM 拉取 ParameterRepository JSON 并嵌入资源。通用 `ParameterMetaData::setEnumFromPairs` 已保留有效项并跳过无效项；新版 APM 代码也对 int8 的 128–255 值按 `code - 256` 解释，可覆盖 `BTN` 动作 200 一类问题。

旧的 skip/clear 解析修复可以退休，但仍要验证实际 Sub 4.8 元数据与动作行为。新版 ParameterRepository 指向浮动 `main`，QGC 标签本身没有冻结其最终内容，应记录构建实际使用的仓库 revision。

### 4.10 FlyView 与遥测布局：5 个，适配

- `src/FlightDisplay/FlyView.qml`
- `src/FlightDisplay/FlyViewBottomRightRowLayout.qml`
- `src/FlightDisplay/FlyViewWidgetLayer.qml`
- `src/FlightDisplay/TelemetryValuesBar.qml`
- `src/QmlControls/HorizontalFactValueGrid.qml`

新版采用 `src/FlyView` 模块。保留地图隐藏的状态所有权、确认交互的层级和转置遥测布局，按新模块组织迁移，避免用旧 FlyView 整体覆盖新版。

### 4.11 仪表空值防护：2 个，保留

- `src/QmlControls/InstrumentValueLabel.qml`
- `src/QmlControls/InstrumentValueValue.qml`

新版仍有对空 `instrumentData`、`factValueGrid` 及字体尺寸的直接访问，当前空值防护没有被完全覆盖。

### 4.12 拍照与视频控件布局：1 个，适配

- `src/FlightMap/Widgets/PhotoVideoControl.qml`

保留当前横向排列意图，合并新版相机相关修复，避免整体恢复旧组件。

### 4.13 手柄按钮与设置入口：2 个，适配

- `src/Vehicle/VehicleSetup/JoystickConfigButtons.qml`
- `src/Vehicle/VehicleSetup/SetupView.qml`

旧文件被 `JoystickComponentButtons.qml` 和 `VehicleConfigView.qml` 等新结构替代。新版按钮页仍有参数存在性查询不随下载状态自动更新、枚举及索引变化、QGC 与固件动作互斥、空值写入等需要复核的路径。保留当前参数就绪后刷新和可选参数缺失时仍可进入手柄设置的意图。

### 4.14 推进器工具入口：1 个，保留并适配

- `src/QmlControls/ParameterEditor.qml`

保留推进器映射工具入口，并按新版参数编辑器结构接入。

### 4.15 视频、连接告警与帮助设置：2 个，适配

- `src/UI/AppSettings/VideoSettings.qml`
- `src/UI/AppSettings/HelpSettings.qml`

新版采用 `src/AppSettings/pages/Video.SettingsUI.json` 等 schema 生成页面。保留 AI、连接告警设置、原生视频禁用时仍可使用的解码器控制和品牌帮助内容，按新版设置所有者接入。

### 4.16 GPS 旧依赖 pin：1 个，退休

- `src/GPS/CMakeLists.txt`

当前 `px4drivers` 的 `0b969588…` pin 用于旧版 pre-Settings 接口。新版 GPSDriver 使用 settings 结构和新的 Driver/CMake pin `cd6f506afda9bd6e8e4645f094dcf5415f1f8be4`，不应继续带入旧 pin。

### 4.17 构建、版本与安装：6 个，适配与部分退休

- `cmake/CreateAppImage.cmake`
- `cmake/CreateCPackNSIS.cmake`
- `cmake/CreateWinInstaller.cmake`
- `cmake/Git.cmake`
- `cmake/Install.cmake`
- `deploy/windows/nullsoft_installer.nsi`

新版迁往 `cmake/install/`、CPack 和 modules/Git 等结构。新版已覆盖 APPVERSION 参数、Publisher、DisplayVersion、InstallLocation 等部分功能；仍需保留产品版本覆盖、品牌图标和桌面链接、AI 分发内容、PE 版本一致性以及安装器返回码处理。

新版 Git 版本逻辑未见当前 `*_OVERRIDE` 处理，不能直接删除定制产品版本入口。新版 NSIS 的旧版卸载路径仍有 `ExecWait` 没有捕获 `$0`、后续却执行 `IntCmp $0` 的问题，当前捕获返回码的修复意图需要保留。

### 4.18 CI：5 个，适配

- `.github/workflows/custom.yml`
- `.github/workflows/windows.yml`
- `.github/workflows/linux.yml`
- `.github/workflows/macos.yml`
- `.github/workflows/crowdin_docs_download.yml`

新版删除了部分旧 workflow，并调整为可复用 action 结构。按新结构保留产品名称、测试和触发条件，不要整体复制旧 workflow。

### 4.19 忽略规则、说明与翻译：4 个，保留内容并适配

- `.gitignore`
- `README.md`
- `translations/qgc_json_zh_CN.ts`
- `translations/qgc_source_zh_CN.ts`

保留项目内容，按新版源字符串合并翻译，保留上游 `%1` 等占位符修复，不能直接用旧 TS 整体替换新版。

### 4.20 测试框架接入：6 个，适配

- `test/CMakeLists.txt`
- `test/UnitTestList.cc`
- `test/Camera/QGCCameraManagerTest.cc`
- `test/Camera/QGCCameraManagerTest.h`
- `test/Vehicle/VehicleLinkManagerTest.cc`
- `test/Vehicle/VehicleLinkManagerTest.h`

新版采用自动注册和发现测试的框架。旧注册样板可退休，测试意图必须保留；测试环境和接口需适配新版，不能把“文件还在”或“被编译”视为“测试已被发现并执行”。

以上各组数量为 `6 + 2 + 3 + 3 + 3 + 3 + 1 + 6 + 3 + 5 + 2 + 1 + 2 + 1 + 2 + 1 + 6 + 5 + 4 + 6 = 65`。

## 5. 插件与 QML 的关键兼容风险

### 5.1 插件退出顺序：优先阻断项

新版 `QGCApplication` 在退出时先调用 core plugin 的 `destroyQmlEngine`，再执行 `cleanup`。当前 `DeepSharkPlugin` 持有原始 `_qmlEngine`、`_selector`，在 `cleanup` 中移除 URL interceptor 并删除 selector，且没有覆盖新版引擎销毁 hook。

因此，当前清理方式直接迁移后可能在引擎已经销毁时访问失效指针。应在新版销毁 hook 中完成与引擎绑定的清理，并验证重复创建、销毁和垃圾回收。原审计未实施修复。官方源码：[QGCApplication 退出流程](https://github.com/mavlink/qgroundcontrol/blob/3a67d31f0c36bf3fe38ec52970d250a89d0aaf67/src/QGCApplication.cc#L761)。

### 5.2 QML 模块和覆盖资源

旧 `QGroundControl.FlightDisplay` 迁往 FlyView；Controllers、Palette、ScreenTools 等 URI 与 C++ 注册组织也有调整，工具栏和设置模块发生变化。

当前自定义 qrc 别名包括 `/Custom/qml/QGroundControl/FlightDisplay/...`，URL interceptor 依赖旧资源路径进行替换。必须核对新模块导入、资源别名和拦截目标。C++ 编译通过不能保证 FlyViewCustomLayer 或四路视频界面实际加载。

## 6. 视频后端与重连的关键兼容风险

### 6.1 显示 sink 接入

旧 `QGCVideoBackground` 在新版中消失，新 `FlightDisplayViewVideoOutput.qml` 使用 QtMultimedia 的 `VideoOutput` 和 `QVideoSink`。

当前 `DeepSharkVideoController` 使用 `setWidget`、`createVideoSink`、`setSink`。这些方法在新版仍存在，问题不能描述成它们被删除。新版创建 sink 时忽略旧 widget 参数，显示连接还需要 `VideoBackend::attachSink`；新版原生 VideoManager 已执行这一过程，当前定制控制器没有对应接入。

需要验证四路显示、布局切换、sink 重建、资源禁用后恢复和退出，不能只验证接收器已启动。

### 6.2 重连所有权

新版 VideoReceiver 默认启用 autoReconnect，并采用 epoch 与指数退避。当前 VideoTile watchdog、重试以及控制器重启也会推动恢复。

升级时需要明确由哪一层负责自动恢复，保留必要状态展示，避免多层重复重连。验收必须覆盖手动停止、清空 URI、切换地址、禁用资源和关闭页面后的待启动任务取消。

### 6.3 RTSP 传输方式

当前 `DeepSharkVideoSettings::streamUrl` 保留摄像头原始 URL，并通过 `rtspt`、`rtspu`、`rtspst`、`rtspsu` 等变体表达传输选择。新版 SourceFactory 设置 UDP/TCP 协议 mask，并使用 GStreamer 的 RTSP URL 解析。

源码可以证明接口变化，不能证明实际摄像头连接必然严格按用户选择走 TCP 或 UDP。需要实际传输观测与断线恢复验证。

## 7. 手柄与用户配置的关键兼容风险

### 7.1 旧配置结构变更

| 旧配置或行为 | 新版配置或行为 |
| --- | --- |
| `Joysticks/<name>` | `JoystickSettingsV2/<name>` 等 SettingsGroup |
| `Calibrated4`、`AxisNMin` 等 | calibrated Fact，默认 false；轴设置数组 |
| `ButtonActionName%1` 等 | 按钮动作数组，含 actionName、repeat |
| manager 的旧 ActiveJoystick 键 | `activeJoystickName` |
| 每 Vehicle 的 joystick enabled | JoystickManagerSettings 的 `joystickEnabledVehiclesIds` |
| 旧 SDL 设备枚举 | SDL3，名称、轴和按钮识别需要实际核对 |

在原审计检查的新版 `src/Joystick`、`src/Settings` 和 QGCApplication 范围内，未找到旧 `Calibrated4` 和完整旧字段的自动迁移逻辑。来源：[新版 Joystick 设置初始化](https://github.com/mavlink/qgroundcontrol/blob/3a67d31f0c36bf3fe38ec52970d250a89d0aaf67/src/Joystick/Joystick.cc#L448)。

两版默认 `QGC_SETTINGS_VERSION` 都为 9。相同版本号意味着不一定自动清空配置，但不代表旧配置已经迁移或能被新版正确使用。

### 7.2 迁移与验收字段

应逐项映射或安排重新校准：轴范围、中心、死区、反转、模式、油门中心和零点、负向控制、hat、按钮动作、repeat、QGC 与固件动作互斥、shift，以及旧全局设置改为每手柄设置的内容。

不能只复制 calibrated 标志。缺少必要字段或检测到设备变化时，应保持未校准状态并要求完成校准。设备上的 `BTN*_FUNCTION`、`BTN*_SFUNCTION` 与本地手柄设置是不同来源，必须分别验证。

### 7.3 视频与 AI 配置

需要保持视频 URL、RTSP 传输选择、四路布局、默认链接和 AI overlay 配置语义。当前 `AIDetection/OverlayEnabled` 与兼容键 `Video/yoloOverlay` 的读取行为，应明确迁移，不因新版设置页面重写而丢失。

## 8. MAVLink、语音、HUD 与控制链路

新版跳过部分 MAVLink v1 非 HEARTBEAT 消息，`RADIO_STATUS` 例外。原讨论中已检查的当天三份飞行日志，其 flight 1/1 链路均为 v2，因此没有发现这些链路的 v1 升级阻断。该结论不能推广到所有其他组件或未观测设备；无需为本文重新执行日志审计。

语音问题按原讨论保持延期。本次源码比较发现新版 AudioOutput 的 volume、muted 初始化、队列和发音处理变化，但未看到足以证明 Windows 语音导致视频卡顿已经修复的线程证据。后续只需安排静音/播报 A/B 测量，不能把源码变化当作问题解决证明。

新版官方 HUD 含 pitch 修正，应核对定制 Attitude3DPanel 与原生 HUD 的方向一致性。AUV 指令与推进器工具还需验证新版 Vehicle、命令 ACK、超时、主动断开、取消和恢复路径。

## 9. 当前开发环境与新版要求

下表是原审计的路径与配置检查结果，没有运行编译器、安装依赖或验证完整运行环境。

| 项目 | 本机原审计观察 | 官方 v5.1.5 配置/处理建议 |
| --- | --- | --- |
| Visual Studio | `D:\Develop\Toolchains\VS2022BuildTools`，MSVC `14.44.35207` x64 | Windows 使用 MSVC 2022；当前安装可作为复用候选 |
| CMake | `C:\Program Files\CMake\bin\cmake.exe`，文件版本 4.1.0 | 新版最低 3.25；可复用候选 |
| Ninja | VS 的 `Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe` 存在 | 未从文件资源取得版本；可复用候选 |
| NSIS | `build-release/nsis-portable/nsis-3.11/Bin/makensis.exe` 及目录根的 makensis 存在 | 目录名为 3.11，未取得文件版本；可复用候选，仍需实际打包验证 |
| Qt | 只发现 `D:\Develop\Toolchains\Qt\6.8.3\msvc2022_64` | minimum 6.11.0，官方目标 6.11.1；需准备独立的新目录 |
| GStreamer | `D:\Develop\Toolchains\GStreamer\1.0\msvc_x86_64`，头文件版本 1.22.12 | Windows/default 目标 1.28.4；旧目录保留，新版依赖独立准备 |
| YOLO Python | `D:\Develop\envs\yolo\Scripts\python.exe` 存在 | 保留环境，可作为复用候选；未验证包与运行状态 |
| 新版构建 Python | 未验证版本与包 | 新生成设置流程及 `.venv` 应使用独立构建环境 |

官方版本配置来源：[v5.1.5 build-config.json](https://github.com/mavlink/qgroundcontrol/blob/3a67d31f0c36bf3fe38ec52970d250a89d0aaf67/.github/build-config.json)。Qt 模块清单为：

```text
qtgraphs qtlocation qtpositioning qtspeech qtmultimedia qtserialport
qtimageformats qtshadertools qtconnectivity qtquick3d qtsensors qtscxml
qtwebsockets qthttpserver
```

GStreamer 通用最低版本 1.20 不能证明本机旧 Windows 依赖组合适用于新版。新版 Windows 自动下载格式需要 1.28 及以上，并有对应校验。

新版其他依赖记录：

- SDL：release 3.4.2。
- GPS Driver：`cd6f506afda9bd6e8e4645f094dcf5415f1f8be4`。
- MAVLink：`c409cf690454db6d3e004bd14173bc6c7ff1e0ff`。
- ArduPilot ParameterRepository：浮动 `main`，必须记录实际解析 revision，并验证 Sub 4.8 元数据。

旧 Debug 和 Release 缓存分别位于 `build-debug-ai/cpm_modules`、`build-release/cpm_modules`，绑定旧 Qt 和当前配置。新版使用独立 build、CPM cache、staging 与依赖目录；不要复用现有 `build-release` CMake 缓存。旧依赖目录保留，运行时只调整目标进程 PATH，不需要修改全局环境。本次没有执行任何环境准备。

## 10. 品牌、安装升级与开发配置隔离

### 10.1 当前产品身份

| 字段 | 当前定制值 |
| --- | --- |
| 应用名称 | `QGC_KevinJiang` |
| 组织名称 | `KevinJiang` |
| 组织域名 | `kevinjiang.local` |
| package identity | `com.kevinjiang.qgc` |
| Windows 图标 | `branding/GRobot_icons/GRobot_taskbar.ico` |
| Windows 版本资源 | `custom/GRobot.rc` |
| 原审计工作区待发布产品版本 | 1.5.0 |
| 正式安装目录 | `$PROGRAMFILES64\QGC_KevinJiang` |
| 卸载注册表身份 | 64 位 HKLM `Uninstall\QGC_KevinJiang` |

自定义产品版本与官方底座 v5.1.5 是不同字段，应分别记录。保留 exe、卸载入口和用户配置 namespace 的升级契约，避免变成一个独立的官方 QGC 安装或丢失当前用户设置。

旧版卸载调用需要正确捕获返回码，并保留 `-LEAVE_DATA=1`。安装器源代码显示保留数据的意图，不等于实际覆盖升级已经验证。

### 10.2 新版开发候选隔离

单独的 exe 目录或 Debug 构建并不自动隔离 QSettings、缓存、日志和保存路径。可以评估既有 Daily 应用名机制或独立开发应用名，但必须从实际启动路径验证最终配置位置。

当前 CustomOverrides 对应用名使用 FORCE，单独传入 `-D` 不一定能改变最终值。新版开发候选启动前应确认身份隔离，避免使用正式配置进行首次试运行。本次未修改这些配置，也未启动应用。

### 10.3 标准发布流程

发布与打包应以 [Windows 发布运行手册](../releases/windows-release-runbook.md) 和项目脚本为入口：

```powershell
tools/release/build-windows-release.ps1 -Version <产品版本号>
```

该脚本目前使用固定 `build-release` 和旧环境假设，迁移后需适配新版路径与隔离环境；其 preflight 也可能写入审计目录，因此本次仅阅读，没有执行。

应监督 `release-state.json`、步骤日志和 `release-report.md`，检查版本、依赖、PE 资源、安装包 hash、品牌身份与 clean PATH 启动。报告中基于源代码得出的 `preservesUserData=true` 仅是契约检查，不是实际安装验证。最终仍需实际覆盖升级、配置保留、卸载失败处理和旧版恢复验证；关键校验未通过或报告未成功时，不建议分发安装包。

## 11. 建议实施顺序与验收门槛

以下是后续升级建议，不代表已获得执行全部步骤的授权或已开始迁移。不需要为本任务创建新分支或工作树；当前项目内使用独立构建目录，开始迁移前重新记录发布完成后的源码基线。

| 优先级 | 工作 | 进入下一步的依据 |
| --- | --- | --- |
| P0 | 当前稳定版发布收尾 | 版本、依赖、安装包 hash 和实际安装检查完成，稳定包可恢复 |
| P1 | 新依赖与独立构建环境 | 新 Qt/GStreamer 等满足要求，旧环境保留，新缓存独立 |
| P1 | 插件退出 hook、QML 模块和自定义资源 | 实际引擎创建/销毁、强制 GC、自定义界面加载通过 |
| P1 | 四路视频与接收器 | URI 清空、重复 pad、解码尺寸、sink 重建、停止取消和单一重连所有者验证通过 |
| P1 | 手柄配置与按钮页面 | 旧配置 fixture、缺失/异常/延迟参数、固件/QGC/shift、重新校准流程验证通过 |
| P2 | ArduSub 元数据与推进器控制 | 实际参数和动作对应，ACK、超时、主动断开、取消与恢复通过 |
| P2 | 品牌与发布升级 | clean PATH 启动、实际安装升级、配置保留和旧版恢复通过 |

## 12. 必要验证清单

### 12.1 离线回归

保留并适配现有五个 DeepShark 测试类：

- `AIDetectionReceiverTest`
- `DeepSharkAuvControllerTest`
- `DeepSharkVideoControllerTest`
- `DeepSharkConnectionMonitorTest`
- `ThrusterDirectControlControllerTest`

同时保留相机晚到回调测试和 VehicleLinkManager 的 MockLink 流量/断开测试，确认新版自动注册实际发现并执行这些测试。新版 Joystick、SDL 和 GStreamer 测试也需按新框架检查。

现有 `DeepSharkVideoControllerTest` 中应保持的回归意图包括：

```text
_rtspTransportPreservesCameraUrls
_videoPanelSavesTransportSelection
_pendingAutoStartIsCancelled
_clearingUriCancelsPendingAutoStart
_clearingActiveReceiverUriStopsPipeline
_duplicateDecoderPadsReleaseParentReference
_videoSinkSizeUsesDecodedCaps
_failureSignalIsIndependentFromDisplayText
_restartRebuildsVideoSink
_failedRestartCanScheduleAgain
_startedRestartCanScheduleAgain
_flyViewStatusQmlLoads
_videoRowsUpdateWithoutReplacingDelegates
_disabledVideoResourcesCanBeRecreated
_coreOptionsSurviveGarbageCollection
```

新版插件销毁顺序还需要针对性的验证，现有测试不能直接证明新退出 hook 安全。

### 12.2 实际设备与安装

- 比赛电脑和实际摄像头：1/2/4 路视频、指定 TCP/UDP、断线恢复、布局切换、关闭、长时间运行；测量帧间隔和延迟。
- 飞控：连接、参数下载、重连、主动关闭、模式、云台、灯光和 failsafe。
- 实际手柄：全部轴、中心、反转、按钮、shift、hat、拔插；验证推进器工具 ACK、取消和恢复。
- 语音：静音与播报 A/B 测量，只报告证据，不扩大为本次修复。
- 安装包：干净运行环境、覆盖旧安装、旧设置迁移、保留配置和旧版恢复。

以上均未在本次审计执行。历史 focused Debug 测试或接收器 probe 的成功，也不能代替新版构建、现场偶发卡顿和完整安装链路的验收。

## 13. 文档固化时的工作区补充说明

用户在只读审计完成后，授权将上述内容保存为项目内 Markdown。本次固化仅新增本文，没有修改源码、构建配置、发布脚本或依赖环境。

固化时 HEAD 仍为 `061e839009dbbd259a050ec3b7eeb708de96ef00`。相较原审计快照，工作区还观察到 `src/MAVLink/LibEvents/CMakeLists.txt` 的修改，以及未跟踪的 `docs/releases/v1.5.0.md`、`docs/releases/v1.5.0.zh-CN.md`。这些是固化前已存在的并行发布变更，本文没有评审或改动它们，也没有将其纳入原审计结论。

后续开始升级时，应从当时实际源码、工作区和发布状态重新建立基线，不能将这份 18:08–18:20 的快照视为一直有效的最新状态。
