# QGC-GRobot 上游补丁登记表（v5.1.5 基线）

> 快照日期：2026-10-11
> 上游基线：`v5.1.5`（`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`）
> 当前分支：`main`，快照提交见本文件的提交记录
> 取代：2026-09-02 的 v5.0.8 基线登记表（只作历史参考，不在本仓库）
> 用途：下次同步上游前判断冲突面、每个补丁为什么存在、最小回归范围和退出方向。

## 1. 使用规则

- 新功能默认放 `custom/`。只有缺少扩展点、必须改变 QGC 核心行为，或修的是上游通用缺陷时才改上游目录。
- 改了既有上游文件就在第 3 节登记，并把文件加进第 4 节。第 5 节的核对命令能查出漏登记的文件。
- 同步新基线时先更新本文件再处理冲突。"能编译"不等于补丁仍然需要。
- 本表只管升级冲突面，不代替发布流程、测试报告和实机验收记录。

## 2. 规模

以 `git diff v5.1.5 --diff-filter=M` 统计，共 118 个既有文件被修改：

| 范围 | 文件数 |
| --- | --- |
| `src/` | 72 |
| `test/` | 37 |
| 构建、安装与仓库级文件 | 7 |
| 翻译 | 2 |

另有约 180 个新增文件（`custom/`、`test/DeepShark/`、`tools/`、`docs/` 等，数量随提交变化，用 `git diff v5.1.5 --diff-filter=A --name-only` 统计）。它们不改既有上游文件，本表只登记对既有文件的修改；上游以后在同一路径新增文件时仍可能冲突。没有删除或改名的上游文件。

本表的目的说明来自两次阅读：2026-10-10 整轮审计时逐文件读过 `src/` 下的全部差异；2026-10-11 写本表时重读了设置页、APM 插件、相机、链路、MockLink、
飞行界面、控件、设置、工具栏、工具类和 `Vehicle` 的差异。手柄、视频 sink 和 D3D11 缓冲这三块本次没有重读，说明取自审计正文。
测试文件只按用途粗分，没有逐个核对每处改动的原因，见 P15。

## 3. 模块清单

| ID | 目的 | 为什么要改上游 | 最小验证 | 退出方向 |
| --- | --- | --- | --- | --- |
| P01 | **版本覆盖、安装包和仓库级文件**：`QGC_APP_VERSION_OVERRIDE`、安装 AI 工具目录、安装器组件化、覆盖安装时处理旧卸载程序、忽略规则、协作说明 | 这些入口都在上游的构建和安装脚本里 | `tools/release/build-windows-release.ps1` 完整成功；覆盖安装 | 产品品牌不回馈上游。能参数化的继续收进 `custom/cmake` |
| P02 | **品牌声明与中文翻译**：帮助页的二次开发声明，两份中文翻译 | 帮助页和翻译是仓库级资源 | 帮助页实际显示；`JsonResourceAuditTest` | 帮助页声明可以改成 custom 覆盖。翻译每次升级都要重做合并 |
| P03 | **设置页挂载点**：通信链路页挂 `ConnectionAlertSettings`，视频页挂 `AIDetectionSettings`，并把"强制解码器"移到单独的 DeepShark 小节 | 设置页由上游 JSON 描述，custom 没有向页面里插入小节的扩展点 | 两个设置页能打开并显示定制小节；设置搜索能搜到 | 上游若提供设置页扩展点就迁出。这两个文件里 `keywords` 被展开成多行是纯格式差异，见第 7 节 |
| P04 | **"禁用安全限制"开关**：新增应用设置 `disableSetupSafetyRestrictions` 和固件能力 `ArmedSetupOverrideCapability`；开启后 ArduSub 在已解锁时可以进配置页、补下载被跳过的参数；传感器和遥控校准在已解锁时拒绝开始；电机页不再无条件上锁。同一批文件里还有两个默认开启的保护开关：`joystickSetupLiveControlGuard`、`allowManualOutputRecovery` | 配置页的可用性判断、初始连接状态机和校准控制器都在上游核心里 | `AppSettingsTest`、`InitialConnectTest`；已解锁时进配置页、尝试校准 | 产品决策，不回馈上游。审计条目 F05、N01、N06、N07 都与它有关 |
| P05 | **ArduSub 新旧参数兼容**：可选的 `FS_GCS_ENABLE`、`FS_GCS_TIMEOUT`、`FS_OPTIONS`；`ARMING_SKIPCHK` 的 ArduSub 默认值不报警；旧 `MNT_` 参数的云台页回退、输出通道分配去重、灯光页按实际通道数 | 这些页面属于上游 APM 插件，整页复制到 custom 会漂移得更厉害 | `APMGimbalCompatibilityTest`；用新旧参数集各打开一次安全、云台、灯光页 | 大部分是通用兼容修复，适合拆成小补丁回馈上游；新基线已有等价实现就删 |
| P06 | **默认 TCP 链路与绕过代理**：加载链路配置时如果没有同名或同端点的配置，就创建 `GRobot_Default`（`192.168.1.200:4019`，自动连接），所以删掉后会被重建；TCP 套接字显式 `NoProxy` | 默认链路的创建发生在 `LinkManager` 加载阶段；代理是底层套接字行为 | 无配置首次启动；已有同名或同端点配置时不改用户设置；有系统代理时能连上 | 默认端点应迁到 custom 的首次运行逻辑。`NoProxy` 是否普适需要单独评估。审计条目 F11 |
| P07 | **手柄**：旧配置迁移到新的设置结构；"禁用安全限制"下配置页可实时控制；按键分配页与校准页的界面重排；水下机器人的轴名称；按键动作对象加锁（N01）；轮询标志和载具指针加锁（N06）；连续动作按控制量记当前持有的按键：改派按键时只结算该按键仍持有的动作并丢弃为旧分配排队的事件，结束对载具的控制时结算仍持有的动作、补做已排队且有归属的松开、丢弃上一会话排队的事件（N07），配置页显示实时控制提示（F05）；摇杆模式选中即保存并重排在用的轴分配，不必重新校准（上游只在完成校准时保存），载具已解锁或手柄控制处于启用状态时拒绝切换（要先取消"启用手柄控制"） | 手柄核心和配置页都在上游 | `JoystickTest`、`JoystickControlTest`、`JoystickManagerTest`、`AppSettingsTest` | 迁移代码在所有用户升级完之后可以删。界面重排是产品改动。N01 的锁修的是本分支引入的并发访问，不回馈 |
| P08 | **飞行界面与工具栏**：定制内容遮住地图时停用地图和比例尺；遥测条转置显示；`InstrumentValue*` 的空数据保护；拍摄控件横向排列；工具栏的解锁/上锁按钮；视图下拉里的退出确认和版本号 | 这些是上游的核心 QML，custom 的覆盖机制只能整文件替换 | `DeepSharkUILayoutTest`（原生窗口方式）、`FlyViewCameraCaptureUITest`、`AppCloseWarningUITest` | 布局类改动可以改成 custom 整文件覆盖，但覆盖文件同样要跟上游合并。空数据保护适合回馈上游 |
| P09 | **输出测试工具入口**：参数页工具菜单增加"输出测试与接线记录"；工具由主窗口创建（`MainWindow.qml` 的 `showDeepSharkThrusterMappingTool()`），不由参数页创建，因为载具离线时配置视图会替换参数页，由它创建的对话框随之失效 | 入口直接插在上游的 `ParameterEditor.qml` | `ThrusterMappingIntegrationTest`；从参数页打开工具 | 可迁成 custom 覆盖或独立入口。审计条目 T07：入口对所有载具类型可见 |
| P10 | **视频管线**：保持"禁用视频"的选择不被 ArduSub 或自动流覆盖；MPEG-TS 低延迟；接收器里解码分支、录像文件 sink 和帧探针的改动；帧在界面线程合并投递；D3D11 多路同尺寸互相覆盖的修复；单路重启不重置共享 GPU 资源；接收器状态标志改为原子；销毁前等工作线程结束（N02）；解码器前的丢帧队列上限由 2 个缓冲改为 100 个缓冲或 16 MiB（上游 v5.1.5 新增的这个队列装的是未解码的数据，丢弃时不区分关键帧；实机上改后花屏消失，是缓解，队列丢关键帧这一点没有直接观测到；真正积压时它能攒下数秒视频才开始丢） | 行为都在上游的接收器、sink 和硬件缓冲里，custom 的四路视频也用同一条管线 | `GStreamerTest`、`GStreamerGstQgcTest`、`GStreamerD3DTest`、`DeepSharkVideoControllerTest`、`DeepSharkVideoResizeTest`；实机四路播放、断线恢复、录像 | 升级时冲突最重的一块。多路覆盖修复、N02 的等待、MPEG-TS 延迟适合回馈上游。审计条目 F02、F06、F18、N02、N08 |
| P11 | **相机信息请求上下文的生命周期**：上下文改为挂在 `Vehicle` 下，避免相机管理器销毁后回调访问已释放内存 | 上游缺陷 | `QGCCameraManagerTest` | 值得回馈上游（审计 F19 的结论） |
| P12 | **MockLink**：工作对象留在链路线程，断开时同步停止；去掉 `Q_ASSERT`；ArduPilot 整型参数按数值存储；参数名长度按字段长度校验 | 测试基础设施在上游目录 | 所有用 MockLink 的测试，重点 `InitialConnectTest`、`ThrusterMappingIntegrationTest` | 线程模型的改动要先确认新基线是否已自行修复。审计条目 T07 的登记要求 |
| P13 | **通用工具修复**：解压时用 UTF-8 设置路径名；`QGCFileHelper` 的 `toLocalPath()` 和 `isLocalPath()` 在交给 `QUrl` 之前先识别绝对路径，避免 Windows 盘符被当成 URL 的协议名 | 上游工具类 | `QGCCompressionTest`、`QGCFileHelperTest` | UTF-8 路径适合回馈上游。这两个文件同时被整文件格式化，见第 7 节 |
| P14 | **主动断开信号和数据到达信号**：`Vehicle::connectionCloseRequested` 区分用户主动断开和心跳超时；`VehicleFactGroup::attitudeAccepted`、`altitudeRelativeAccepted` 在姿态和相对高度真正被采纳时发出，数值没变也发 | 断联预警和 3D 姿态面板要用。哪条消息被采纳只有 `Vehicle` 自己知道（它在两种姿态消息、两种高度消息之间有优先和锁存） | `DeepSharkConnectionMonitorTest`、`Attitude3DPanelTest` | 上游若提供等价信号就删 |
| P15 | **上游测试的适配与补充**：为上面各模块补的用例；升级时为严格日志检查、Windows 路径、字体和时序做的适配；`test/CMakeLists.txt` 注册 `test/DeepShark` | 测试树在上游目录 | 各自的定向运行 | 适配类改动每次升级都要重新核对。有一处是放宽：`ParameterManagerTest` 把"首个进度信号及时到达"改成了下载完成后再检查 |

## 4. 文件覆盖

每个文件只列一次，按主要责任归组。

### P01 版本、安装与仓库级文件（7）

- `.gitignore`
- `AGENTS.md`
- `CMakeLists.txt`
- `README.md`
- `cmake/install/CreateWinInstaller.cmake`
- `cmake/install/Install.cmake`
- `deploy/windows/nullsoft_installer.nsi`

### P02 品牌与翻译（3）

- `src/AppSettings/HelpSettings.qml`
- `translations/qgc_json_zh_CN.ts`
- `translations/qgc_source_zh_CN.ts`

### P03 设置页挂载点（2）

- `src/AppSettings/pages/CommLinks.SettingsUI.json`
- `src/AppSettings/pages/Video.SettingsUI.json`

### P04 "禁用安全限制"开关（15）

- `src/AppSettings/pages/General.SettingsUI.json`
- `src/AutoPilotPlugins/APM/APMSensorsComponentController.cc`
- `src/AutoPilotPlugins/APM/APMSensorsComponentController.h`
- `src/AutoPilotPlugins/APM/APMSubMotorComponent.qml`
- `src/FirmwarePlugin/APM/ArduSubFirmwarePlugin.cc`
- `src/FirmwarePlugin/FirmwarePlugin.h`
- `src/QmlControls/SetupPage.qml`
- `src/Settings/App.SettingsGroup.json`
- `src/Settings/AppSettings.cc`（另含语言列表里"System"的翻译）
- `src/Settings/AppSettings.h`
- `src/Vehicle/InitialConnectStateMachine.cc`
- `src/Vehicle/InitialConnectStateMachine.h`
- `src/Vehicle/Vehicle.cc`（另含 P10 的"不覆盖禁用视频"和 P14 的主动断开信号）
- `src/Vehicle/Vehicle.h`（另含 P14）
- `src/Vehicle/VehicleSetup/RemoteControlCalibrationController.cc`

### P05 ArduSub 参数兼容（7）

- `src/AutoPilotPlugins/APM/APMFailsafesComponentSummarySub.qml`
- `src/AutoPilotPlugins/APM/APMFlightSafetyComponentSub.qml`
- `src/AutoPilotPlugins/APM/APMFlightSafetyComponentSummarySub.qml`
- `src/AutoPilotPlugins/APM/APMGimbalInstance.qml`
- `src/AutoPilotPlugins/APM/APMGimbalParams.qml`
- `src/AutoPilotPlugins/APM/APMLightsComponent.qml`
- `src/FirmwarePlugin/APM/APMMainStatusIndicator.qml`

### P06 默认 TCP 链路与绕过代理（3）

- `src/Comms/LinkManager.cc`
- `src/Comms/LinkManager.h`
- `src/Comms/TCPLink.cc`

### P07 手柄（11）

- `src/Joystick/Joystick.cc`
- `src/Joystick/Joystick.h`
- `src/Settings/JoystickManagerSettings.cc`
- `src/Settings/JoystickSettings.cc`
- `src/Vehicle/VehicleSetup/JoystickComponent.qml`
- `src/Vehicle/VehicleSetup/JoystickComponentButtonMonitor.qml`
- `src/Vehicle/VehicleSetup/JoystickComponentButtons.qml`
- `src/Vehicle/VehicleSetup/JoystickComponentSettings.qml`
- `src/Vehicle/VehicleSetup/JoystickConfigController.cc`
- `src/Vehicle/VehicleSetup/JoystickConfigController.h`
- `src/Vehicle/VehicleSetup/RemoteControlCalibration.qml`

### P08 飞行界面与工具栏（12）

- `src/FlightMap/Widgets/PhotoVideoControl.qml`
- `src/FlyView/FlyView.qml`
- `src/FlyView/FlyViewBottomRightRowLayout.qml`
- `src/FlyView/FlyViewWidgetLayer.qml`
- `src/FlyView/TelemetryValuesBar.qml`
- `src/MainWindow/MainWindow.qml`
- `src/QmlControls/HorizontalFactValueGrid.qml`
- `src/QmlControls/InstrumentValueLabel.qml`
- `src/QmlControls/InstrumentValueValue.qml`
- `src/Toolbar/FlyViewToolBar.qml`
- `src/Toolbar/MainStatusIndicator.qml`
- `src/Toolbar/SelectViewDropdown.qml`

### P09 输出测试工具入口（1）

- `src/QmlControls/ParameterEditor.qml`

### P10 视频管线（11）

- `src/VideoManager/VideoManager.cc`
- `src/VideoManager/VideoReceiver/GStreamer/GstSourceFactory.cc`
- `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.cc`
- `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.h`
- `src/VideoManager/VideoReceiver/GStreamer/HwBuffers/common/HwBuffers.cc`
- `src/VideoManager/VideoReceiver/GStreamer/HwBuffers/d3d/GstD3D11VideoBuffer.cc`
- `src/VideoManager/VideoReceiver/GStreamer/MockVideoStreamServer.cc`
- `src/VideoManager/VideoReceiver/GStreamer/QGCQVideoSinkController.cc`
- `src/VideoManager/VideoReceiver/GStreamer/QGCQVideoSinkController.h`
- `src/VideoManager/VideoReceiver/GStreamer/gstqgc/gstqgcqvideosink.cc`
- `src/VideoManager/VideoReceiver/VideoReceiver.h`

### P11 相机信息请求上下文（2）

- `src/Camera/QGCCameraManager.cc`
- `src/Camera/QGCCameraManager.h`

### P12 MockLink（3）

- `src/Comms/MockLink/MockLink.cc`
- `src/Comms/MockLink/MockLink.h`
- `src/Comms/MockLink/MockLinkWorker.cc`

### P13 通用工具修复（2）

- `src/Utilities/Compression/QGClibarchive.cc`
- `src/Utilities/FileSystem/QGCFileHelper.cc`

### P15 上游测试的适配与补充（37）

为本分支的改动补用例的：

- `test/CMakeLists.txt`
- `test/Camera/QGCCameraManagerTest.cc`
- `test/Camera/QGCCameraManagerTest.h`
- `test/Joystick/CMakeLists.txt`
- `test/Joystick/JoystickTest.cc`
- `test/Joystick/JoystickTest.h`
- `test/QmlUITests/AppCloseWarningUITest.cc`
- `test/QmlUITests/FlyViewCameraCaptureUITest.cc`
- `test/QmlUITests/FlyViewCameraCaptureUITest.h`
- `test/Settings/AppSettingsTest.cc`
- `test/Settings/AppSettingsTest.h`
- `test/Utilities/Compression/QGCCompressionTest.cc`
- `test/Utilities/FileSystem/QGCFileHelperTest.cc`
- `test/Utilities/Parsing/Json/JsonResourceAuditTest.cc`
- `test/Utilities/Parsing/Json/JsonResourceAuditTest.h`
- `test/Vehicle/InitialConnectTest.cc`
- `test/Vehicle/InitialConnectTest.h`
- `test/Vehicle/VehicleLinkManagerTest.cc`
- `test/Vehicle/VehicleLinkManagerTest.h`
- `test/VideoManager/GStreamer/GStreamerTest.cc`
- `test/VideoManager/GStreamer/GStreamerTest.h`
- `test/VideoManager/GStreamer/HwBuffers/common/GStreamerHwBuffersCommonTest.cc`
- `test/VideoManager/GStreamer/HwBuffers/d3d/GStreamerD3DTest.cc`
- `test/VideoManager/GStreamer/gstqgc/GStreamerGstQgcTest.cc`

升级时为了在本机通过而适配的（原因没有逐个核对；其中 `PX4ParameterMetaDataTest.cc` 和 `QGCMAVLinkTest.cc` 的差异大部分是整文件格式化）：

- `test/Comms/Bluetooth/BluetoothWorkerTest.cc`
- `test/Comms/LinkConfigurationTest.cc`
- `test/FactSystem/FactMetaDataTest.cc`
- `test/FactSystem/PX4ParameterMetaDataTest.cc`
- `test/FactSystem/ParameterManagerTest.cc`
- `test/MAVLink/QGCMAVLinkTest.cc`
- `test/MAVLink/Signing/SigningControllerTest.cc`
- `test/MAVLink/StatusTextHandlerTest.cc`
- `test/MAVLink/SysStatusSensorInfoTest.cc`
- `test/MissionManager/QGCMapPolygonTest.cc`
- `test/Utilities/Parsing/Json/JsonHelperTest.cc`
- `test/Utilities/Parsing/Json/JsonParsingTest.cc`
- `test/Utilities/Platform/PlatformTest.cc`

### P14 主动断开信号和数据到达信号（2）

- `src/Vehicle/FactGroups/VehicleFactGroup.cc`
- `src/Vehicle/FactGroups/VehicleFactGroup.h`

主动断开信号和相对高度的另一处到达通知在 `Vehicle.cc`、`Vehicle.h` 里，这两个文件列在 P04 下。

## 5. 核对命令

登记是否完整，用下面的命令核对（输出应为空）：

```bash
git diff v5.1.5 --diff-filter=M --name-only | while read f; do grep -qF -- "\`$f\`" docs-deepshark/upstream-patches.md || echo "未登记: $f"; done
```

## 6. 升级时要核对的隐性依赖

这些地方通常不会以既有文件的合并冲突暴露出来，上游一改就可能悄悄失效：

- `custom/src/ThrusterDirectControlController.cc` 连接了 `ParameterManager` 的四个下划线开头的内部信号：`_paramSetSuccess`、`_paramSetFailure`、
  `_paramRequestReadSuccess`、`_paramRequestReadFailure`。上游改名会编译失败；改了发出时机或参数含义则只会表现为直测的回读、恢复判断出错。
  升级后跑 `ThrusterDirectControlControllerTest` 和 `ThrusterMappingIntegrationTest`（审计条目 T05）。
- `custom/src/ThrusterDirectControlController.cc` 的 `pendingRecoveryNotice()` 直接读 `ThrusterMappingTool.qml` 里 `Settings` 对象写的键
  （分组 `DeepSharkServoOutputMapping`）。改键名要两边一起改。
- `tools/ai_detection/yolo_to_qgc_udp.py` 的 `MAX_DETECTIONS_PER_PACKET` 要与 `custom/src/AIDetectionReceiver.cc` 的 `kMaxDetectionsPerPacket` 一致；
  `run_yolo_to_qgc_auto.py` 直接读 QGC 配置文件里的 `Video\CameraNUrl`、`Video\CameraNEnabled`。
- `src/QmlControls/ParameterEditor.qml`、`src/MainWindow/MainWindow.qml` 和两个设置页 JSON 用资源路径引用 `custom/` 里的 QML，路径随上游的 QML 模块布局变化。
- `custom/src/ThrusterDirectControlController.cc` 的 `_writeFunction()` 在直测和恢复流程里写 `SERVOn_FUNCTION` 时，临时清掉该参数元数据上的"需要重启载具"标记，写完再恢复，以免上游弹出"立即重启载具"的对话框（v5.1.5 新增，由 `Fact::setRawValue()` 经 `containerRawValueChanged` 同步触发）。
  上游如果把这个提示改成延迟触发，或改用别的判断来源，对话框会重新出现在直测中途。`ThrusterMappingIntegrationTest` 的严格日志检查会因多出的提示而失败。
- 反过来，`custom/src/DeepSharkPlugin.cc` 的拦截器用 `:/Custom` 加上游资源路径来覆盖上游 QML，并依赖引擎销毁钩子把拦截器摘掉
  （`QGCApplication.cc` 的关闭流程调用它）。上游的资源布局或引擎创建销毁流程变了，覆盖会失效或寿命出错。
- `QGCQVideoSinkController.cc` 用了 Qt 的私有头文件 `qquickvideooutput_p.h`，并读取自定义 sink 的 `frames-delivered` 属性。
  Qt 小版本升级也可能改私有接口。
- AI 检测的通道标识：`run_yolo_to_qgc_auto.py` 生成 `deepSharkVideoN`，`FourVideoPanel.qml` 用同样的名字做 `receiverName`，
  `VideoTile.qml` 按它筛选结果，`AIDetectionReceiver.cc` 解析 `source_id`。名字或消息字段对不上时两边都能编译，只是有包却没有叠加层。
- QML `Settings`、C++ `QSettings` 和 Python 读的是同一个配置文件：依赖应用标识和 INI 格式（`QGCApplication.cc` 里设置）、
  `Settings` 的属性写入延迟（恢复记录因此要显式写入，见审计条目 N09）以及 INI 的字符串编码（百分号问题见 N10）。只对键名是不够的。

## 7. 纯格式差异

有四个文件被整文件格式化到 clang-format 零违规：`QGCFileHelper.cc`、`QGClibarchive.cc`、`PX4ParameterMetaDataTest.cc`、`QGCMAVLinkTest.cc`。
官方 v5.1.5 的版本分别有 124、6、92、52 处违规（用仓库固定的 clang-format 22.1.5 检查）。原因是仓库的提交钩子对改过的文件做整文件检查。

这带来两条规则的冲突：还原成官方格式能把这四个文件的差异降到实际的功能改动，升级时少冲突；但只要文件里还有功能改动，钩子就会重新报格式错误。
同一批里的 `TCPLink.cc`、`Vehicle.cc`、`JoystickConfigController.cc`、`Joystick.h` 没有被格式化（各有几十到上千处官方原有的违规）也提交了，说明钩子没有一致地执行。

另外 `CommLinks.SettingsUI.json` 和 `Video.SettingsUI.json` 的 `keywords` 被展开成多行，`TCPLink.cc` 的 include 被重排，都没有功能意义。

怎么处理是一个待定的决策（审计条目 F10）：要么接受这些格式差异并继续让钩子通过，要么还原并给上游文件免除整文件格式检查。
