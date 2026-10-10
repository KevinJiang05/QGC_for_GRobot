# 2.0.0 全量审查清单（只读）

日期：2026-10-10。审查对象为 `main` 的 `d736dd1fe`；推进器部分审的是其后的 `f8c7584d6`，基线为官方 `v5.1.5` 标签，
升级前行为对照 `db9c91596`。本轮只读代码、差异和已有审计记录，**没有修改代码、
没有编译、没有运行测试**。

所有条目的证据等级均为"读码推断"，除非另有说明。条目交 Codex 二次复核后再决定是否修改。
Codex 复核会话：`01a12672-65cf-7d60-b8c1-6e27a1c1d488`（2026-10-10 启动，整轮清单一次性交复核）。
提示词与输出保留在 `.tmp/codex/full-review-20261010/`。

## 覆盖范围

第一版清单在只读完高风险部分时就写成了"整轮完成"，这是不准确的。下表是补读之后的实际状态。

| 范围 | 状态 |
| --- | --- |
| `src/` 相对 v5.1.5 的 70 个文件差异 | 全部已读；`MockLink` 三个文件只读了增删行 |
| 视频链路补丁（sink、sink 控制器、接收器、D3D11 缓冲、`HwBuffers`） | 已读 |
| `custom/src` 全部 C++（9 组 `.cc/.h`） | 整文件已读 |
| `custom/src` QML：`FlyViewCustomLayer`、`FourVideoPanel`、`VideoTile`、`DeepSharkStatusPanel`、`ThrusterMappingTool`、`Attitude3DPanel`、`Attitude3DViewState`、`AIDetectionVideoOverlay`、`ConnectionAlertBanner`、`ConnectionAlertSettings` | 整文件已读 |
| `AuvMissionPanel.qml`（897 行）、`AIDetectionSettings.qml`（267 行） | 只检索了载具访问、指令、定时器和文件操作，未逐行读（演示功能界面） |
| `test/DeepShark/` 11 个测试 | 9 个整文件已读；`Attitude3DPanelTest`、`DeepSharkVideoResizeTest` 读了用例清单、方向约定和全部断言行 |
| 升级时改过的上游测试（17 个有删改的文件） | 逐个读了差异 |
| 上游测试里的纯新增用例（`JoystickTest`、`InitialConnectTest`、`AppSettingsTest`、`GStreamer*Test`、`FlyViewCameraCaptureUITest` 等） | `JoystickControlTest` 读了后两个用例；其余只看了用例名，未读 |
| 构建与安装：根 `CMakeLists.txt`、`cmake/install`、NSIS、`custom/cmake`、`custom/CMakeLists.txt` 的差异 | 已读 |
| `tools/release/build-windows-release.ps1` | 整文件已读 |
| `tools/release/test-windows-release.ps1`（371 行） | 未读 |
| `.github/workflows/kevin-windows-debug.yml` 及远端运行记录 | 已读、已查 |
| `tools/ai_detection`：`run_yolo_to_qgc_auto.py` | 读了前 200 行 |
| `tools/ai_detection` 其余三个脚本和测试 | 未读（演示功能） |
| 翻译 | 统计了完成度，机械检查了全部已完成条目的占位符；未做语义抽查 |
| 修复记录里的声明 | 抽查了 7 条，见"声明核对" |
| 启停脚本、`software/`、`Ultralytics/`、通用无人机功能 | 按约定不审 |

方法上原计划的三步里：逐文件台账以上表代替，没有单独生成；用测试坐实疑点没有做（本轮约定只审不改，新增用例属于改动）；声明核对做了抽查。

## 现场症状：切换操控模式时画面偶发卡顿

**已确认的机制。** 每一帧视频都要回到 GUI 线程才能显示：
`gstqgcqvideosink.cc:153` 在 `QVideoSink` 所属线程调用 `setVideoFrame()`。
GUI 线程被任何事情占住多久，全部视频就停多久。这一点官方 v5.1.5 也一样。

**定制代码里与飞行模式有关的只有一处**：状态面板的一行文字
（`FlyViewCustomLayer.qml:64`、`DeepSharkStatusPanel.qml:234`）。它不改变视频区域几何，
不触发重连。视频面板的右边距取自固定的 `panelWidth`，与工具栏文字宽度无关。

**最可疑的是模式播报。** `Vehicle.cc:1824` 在每次模式变化时调用语音播报，
`AudioOutput.cc:254` 把入队操作投递给语音引擎对象。按代码看引擎对象随应用静态对象创建在 GUI 线程，
调用方也在 GUI 线程，所以这是一次 GUI 线程上的同步调用（线程归属未用调试器核实）。另外有两点会放大问题：

- 中文界面下播报文本是 `手动manual飞行模式`、`自稳飞行模式`
  （`qgc_source_zh_CN.ts:21673` 及 ArduSub 模式名译文），而 `AudioOutput.cc:187`
  在系统装有 `en_US` 语音时会把引擎固定为 `en_US` 和第一个可用音色。让英文音色念中文，行为取决于系统语音引擎。
- 快速来回切换会让队列累积，超过 20 条时 `AudioOutput.cc:256` 执行立即停止，这也是同步调用。

语音引擎在 Windows 上启动一次播报到底阻塞多久，读代码无法确定。**这是假设，不是结论。**

**两分钟就能区分的实验**：在"应用程序设置 → 常规"里把语音静音，然后同样方式切换模式 20 次。
卡顿消失就是它；不消失的话请告诉我模式是怎么切的（手柄按键 / 工具栏下拉），我再沿另一条链路查。
如果是工具栏下拉，下拉面板本身是动态创建的，那是另一个候选。

## 发现

严重度：S0 可能导致非预期的执行器动作或失控；S1 现场会崩溃、卡死或功能失效；
S2 行为错误或有现场影响但有绕过办法；S3 维护和升级成本。
没有发现 S0。S1 有一条，在推进器直测功能里。除 F23 来自远端运行记录外，其余均为读码推断。

### S1（推进器 PWM 直测）

先说明这个功能的性质：它在飞控**上锁**状态下把某一路输出改成 Disabled，再用 `DO_SET_SERVO` 直接给 PWM。
这条路径绕过了 ArduSub 的解锁和失控保护，停转完全依赖 QGC 随后发出的回中指令。
已有的保护是到位的：只允许上锁时开始、时长上限 5 秒、默认 1550、开始前回读原功能并先落盘恢复记录、
解锁即中止、回中确认后才恢复功能参数。界面上也已写明"直接输出的时长由地面站计时；通信中断时可能无法回中，
请准备独立的断电停止手段"（`ThrusterMappingTool.qml:1711`）。所以这里不是没人意识到的隐患，而是已声明的限制里还能由软件收窄的部分。

**T01 回中指令只发一次；心跳中断后不再尝试回中，链路恢复也不会自动恢复。**
- `DO_SET_SERVO` 在 QGC 的指令队列里不重试，回执超时 1.2 秒（`MavCommandQueue.cc:197`、`:331`）。
  回中指令的回执没收到，就直接进入"需要人工恢复"（`ThrusterDirectControlController.cc:504-510`、`:669-670`），不会再发第二次。
- 心跳中断时，`_vehicleUnavailable()`（`:586-598`，由 `:66-70` 触发）直接转入"需要人工恢复"并停掉全部定时器，
  包括测试时长定时器。此时如果测试 PWM 已经发出，没有任何代码再尝试回中。
- 链路恢复后没有自动动作，必须由人点"恢复异常通道"。

后果：链路抖动恰好落在测试窗口内时，推进器会按测试 PWM 一直转，直到有人发现并手动恢复。
建议：进入"需要人工恢复"且曾发出过测试 PWM 时，持续按固定间隔尽力发送回中；链路恢复后自动执行恢复流程。
前提（未实机核实）：ArduPilot 对功能为 Disabled 的输出会保持最后一次 `DO_SET_SERVO` 的值，没有超时。

测试侧的情况：`ThrusterMappingIntegrationTest::_missingNeutralAckKeepsQmlRecoveryJournal` 断言回中回执丢失时总共只发出 2 条 `DO_SET_SERVO`，
等于把"回中只发一次"固化成了预期行为；"正在输出时心跳中断"没有任何用例覆盖。

### S2

**T02 整个直测没有飞控侧的超时。**
QGC 进程崩溃、被关闭或电脑休眠时，结果同 T01，而且这种情况下 QGC 自己无法补救。界面已明示这一限制。
可评估改用带飞控侧自动回位的指令（例如 `MAV_CMD_DO_REPEAT_SERVO`，由飞控在设定时间后回到 trim），
这需要对照 ArduSub 版本确认其对 Disabled 输出的行为，属于设计层面的决定。

**F01 视频自动重连 6 次后永久放弃。**
`VideoTile.qml:42` 的 `maxAutoRetries: 6`，退避序列为 2.5、5、10、20、30、30 秒
（`VideoTile.qml:229-234`）。加上每次 8 秒启动超时，相机或交换机掉电约两分半钟后，
该路视频进入"连接失败"并不再自动恢复，必须手动点"重连"。`retryCount` 只在重新出图时清零。
对缆控水下机器人，建议改为不限次数、退避封顶。需要你确认这是不是有意设计。

**F02 帧计数取自解码输出，不是实际显示。**
`DeepSharkVideoController.cc:603-604` 的探针挂在 sink 的输入 pad 上。
画面年龄 watchdog（`VideoTile.qml:173`）和断联预警（`DeepSharkConnectionMonitor.cc:269`）
都用这个计数。如果解码正常但纹理映射失败或 GUI 不取帧，画面是冻住的，FPS 仍显示正常，
watchdog 和预警都不会触发。sink 自己有 `frames-delivered` 计数（GUI 实际取帧数），可以改用它。
反过来看，现在的做法好处是 GUI 卡顿不会误报视频断流。

**F03 断联预警要等"飞控 + 全部已启用视频"都就绪才开始工作。**
`DeepSharkConnectionMonitor.cc:298-313`。只要有一路已启用的相机从启动起就没出过图，
监测永远不武装，此后飞控心跳丢失也不会触发这个预警（QGC 自带的通信丢失提示不受影响）。
这个行为有测试固化（`DeepSharkConnectionMonitorTest::_startupRequiresStableLinks`），
是设计取舍而不是疏漏。是否让飞控心跳单独武装，请你决定。

**F04 按键分配页：在"地面站动作"列选任意一项都会把该按键的固件功能和 Shift 功能写成 0。**
`JoystickComponentButtons.qml:111-112`。包括重新选一次 `No Action`。
现在三列并排显示，用户很容易以为它们互相独立。这是对飞控参数的直接写入，
开启"禁用安全限制"后在解锁状态下也会发生。升级前的旧代码（已注释掉的那段）有同样的互斥逻辑，
但旧界面是单个下拉框，不存在这个误解。
`JoystickControlTest::_firmwareButtonOwnership` 断言了"选择一个真实的地面站动作会清零固件功能"，说明互斥本身是有意的；
重选 `No Action` 的情况没有用例覆盖。

**F05 开启"禁用安全限制"后，手柄配置页里按按键会真实执行其动作。**
`Joystick.cc:800`、`Joystick.cc:986`。审计记录写明这是有意的（配置期间继续发送控制）。
但"通用"页的提示是"按下按键检查响应"，"按键分配"页的提示是"按下按键高亮所在行"，
用户按键识别位置时，解锁/上锁、模式切换、灯光等动作会同时发给机器人。
请确认这是你要的行为；如果是，建议在页面上写明。

**F06 D3D11 路径每帧新建并销毁一张整帧纹理。**
`GstD3D11VideoBuffer.cc:43`。官方实现是每种尺寸 3 张的全局循环池；
`bd2d7b0db` 为修多路同尺寸互相覆盖的问题（这个问题是真的，值得回馈上游）改成了每帧分配。
硬件解码输出几乎总是纹理数组，所以这条路径每帧都走。三路 30 帧就是每秒约 90 次
1080p 纹理的创建、拷贝、刷新和释放，全部在流线程上、持有设备锁。
短时间看不出问题，长时间运行的显存碎片和驱动行为没有验证过。
更稳妥的做法是按 sink 实例各持一个小池。建议实机跑一小时看 GPU 专用内存曲线。

**F07 解锁状态下重连后，工具栏的解锁/上锁按钮可能不可用。（未核实）**
`FlyViewToolBar.qml:108-117` 的 `enabled` 要求参数已就绪且通信未丢失。
QGC 在载具已解锁时连接会跳过参数下载（`InitialConnectStateMachine.cc:104`），
除非开启了"禁用安全限制"。如果跳过后参数状态保持未就绪，那么 QGC 崩溃重启、
机器人仍在水里解锁的场景下，这个按钮是灰的。官方版本把按钮放在同样受参数就绪限制的面板里，
限制是继承来的，但现在它是主界面上唯一的上锁入口。我没有读完参数就绪的全部赋值路径，
需要用 MockLink 验证。

**F08 模式播报在 GUI 线程执行。** 见上一节，待实验确认。

**F23 远端 CI 从建立至今没有成功过一次，从未编译或运行过任何测试。（已核实）**
`Kevin Windows Debug` 工作流在 `KevinJiang05/QGC_for_GRobot` 上共 8 次运行，全部失败，
包括 2.0.0 的提交 `d736dd1fe` 和今天的 `bead00b7c`。每次都停在"Build setup"里的 Qt 安装：
`aqt` 报 `Failed to locate XML data for Qt version '6.11.1'`，后面的配置、编译、测试步骤全部跳过。
也就是说目前没有任何自动门禁在起作用，所有"测试通过"的结论都只来自本机。
10-06 的修复记录写的是"新工作流尚未推送和远端执行"，之后没有人回头看过结果。

**T03 恢复记录只在打开这个工具时才看得到。**
恢复记录存在 `DeepSharkServoOutputMapping` 设置组里，全仓库只有 `ThrusterMappingTool.qml` 读它。
QGC 在直测中途崩溃，或者载具断开后关掉了工具，那一路输出会留在 Disabled。
重启后主界面没有任何提示，下次下水那个推进器不工作，操作者不知道原因。
建议启动时或连上载具时检查这条记录并弹出提示。

**T04 飞控不上报 UID 时，QGC 重启后无法用工具恢复。**
`ThrusterMappingTool.qml:722-737`。重启后只能靠 UID 匹配恢复记录；UID 为空时判定为不匹配，
工具拒绝恢复，并允许"忽略记录"。这时只能去参数页手动改回 `SERVOn_FUNCTION`。
需要确认你们的飞控是否上报 UID（工具标题栏会显示"UID 未上报"）。

**F21 默认常开的 3D 姿态预览以显示器刷新率持续重绘。**
右侧预览默认是 3D 姿态（`FlyViewCustomLayer.qml:46`）。`Attitude3DPanel.qml:83-93` 的帧动画只要有深度或姿态数据就一直运行，
每帧更新水面相位，迫使整个 3D 场景每帧重画；场景开了高质量多重采样和阴影（`Attitude3DPanel.qml:165-166`、`:182`）。
它和三路视频共用同一个渲染线程和 GPU。在集成显卡的笔记本上，这可能是视频流畅度的常驻负担。
验证方法：把右侧预览切到"RTSP 1"，对比任务管理器里的 GPU 占用。

### S3

**T05 控制器依赖参数管理器的内部信号。**
`ThrusterDirectControlController.cc:78-126` 连接的是 `_paramSetSuccess`、`_paramSetFailure`、
`_paramRequestReadSuccess`、`_paramRequestReadFailure`，都是带下划线前缀的内部信号。上游改名或改语义时这里会静默失效或编译失败。

**T06 `requestRestore` 和 `reportParameterUnavailable` 对界面层公开，且不检查是否已回中。**
`ThrusterDirectControlController.cc:552-584`。目前 QML 没有调用它们，所以不是现存缺陷；
但在"正在输出"状态调用 `requestRestore` 会跳过回中直接写回功能参数。建议改为私有或加状态检查。

**T07 入口对所有载具类型可见，MockLink 补丁未登记。**
参数页的"输出测试与接线记录"菜单项没有按载具类型限制（`ParameterEditor.qml`）。
`MockLink.cc` 对 ArduPilot 整型 `PARAM_SET` 的存储方式改动属于上游测试设施，
对其他使用 ArduPilot 模拟飞控的上游用例有没有影响，我没有跑全量测试核实。
升级时 `MockLink` 还把工作对象从独立线程改成了与链路同线程，并把断言换成了防御性返回；
这些都改变了上游测试设施的行为，同样没有登记。

**F09 上游补丁登记表已失效。**
`upstream_patch_inventory_2026-09-02.md` 没有任何 v5.1.5 或 10-06 之后的内容，
仍以 v5.0.8 为基线。10-05 的验证记录写 `src/` 差异 36 个文件，现在是 70 个。
新增的手柄页重写、解锁状态编辑、工具栏解锁按钮、视频 sink 重写、D3D11 改动、
云台旧参数兼容都没有登记目的、退出策略和验证方式。下次升级要从头考古。

**F10 无功能的格式化差异混在补丁里。**
`QGCFileHelper.cc` 忽略空白后仍有 +60/−40，实际功能改动约 9 行；
`QGClibarchive.cc`、`TCPLink.cc`、`Joystick.h`、`JoystickConfigController.cc` 的 include 重排，
`Vehicle.cc` 三个单行函数被展开，`CommLinks.SettingsUI.json` 和 `Video.SettingsUI.json`
的 `keywords` 数组被展开成多行。上游测试也一样：`PX4ParameterMetaDataTest.cc`（+78/−61）、`QGCMAVLinkTest.cc`（+68/−61）
的差异绝大部分是指针写法和对齐的重排。这些都会在下次合并上游时制造冲突，建议还原。

**F11 默认链路删不掉。**
`LinkManager.cc:444`。每次启动只要找不到同名或同端点的 TCP 配置就重新创建
`GRobot_Default` 并设为自动连接。用户删除后重启会复活。登记表 U07 已把它列为待迁出项。

**F12 云台旧参数页的副作用写入。**
`APMGimbalInstance.qml:236-242`。任何一个 `MNT_RC_IN_*` 的值变化都会把 `MNT_DEFLT_MODE` 写成 3，
包括从参数页或飞控侧发生的变化，不只是用户在这个下拉框上的操作。只影响旧固件。

**F13 未核实的常量。**
`APMFlightSafetyComponentSub.qml:248` 和对应的摘要页把 `ARMING_SKIPCHK == 0x00fffe3f`
视为"ArduSub 默认"并隐藏"关闭解锁检查"的警告。我没有找到这个数值的出处，需要对照固件确认。

**F14 断联监测每 500 毫秒无条件发出状态变化信号。**
`DeepSharkConnectionMonitor.cc:312`、`:342`。它是 7 个属性的通知信号，
所有绑定每秒重算两次。开销很小，但没有必要。

**F15 定制设置绕过 QGC 设置体系。**
`DeepSharkVideoSettings`、`AIDetectionManager`、`DeepSharkConnectionMonitor` 直接读写 `QSettings`，
没有用 `SettingsGroup`/`SettingsFact`。因此不出现在设置搜索里，
也不受"禁用全部数据持久化"开关约束。类型注册用的是 `qmlRegisterType`，不是项目规范要求的 `QML_ELEMENT`
（`DeepSharkPlugin.cc:55-57`）。

**F16 函数内静态 QObject。**
`DeepSharkPlugin.cc:48-49` 的 AUV 控制器和视频设置对象在进程退出时、应用对象销毁之后才析构。
目前没有看到崩溃证据，属于退出顺序上的隐患。

**F17 Windows 上停止 AI 总是 3 秒后强杀。**
`AIDetectionManager.cc:430`、`:470`。`QProcess::terminate()` 对没有窗口的控制台进程无效，
Python 侧注册的信号处理（`run_yolo_to_qgc_auto.py:126-128`）收不到。
结果是点停止后进程再跑 3 秒，退出应用时界面多等 3 秒。演示功能，优先级低。

**F18 单路重启不再重置共享 GPU 缓存。**
`HwBuffers.cc:180`。改动理由成立（避免一路重启清掉其他路的资源）。
代价是：如果共享设备处于异常但没有上报设备丢失的状态，重连不再能自愈，只能重启应用。记录备查。

**F19 相机信息请求上下文随载具存活。**
`QGCCameraManager.cc:125`。修的是真实的释放后访问，可以回馈上游。
副作用是相机管理器每重建一次就多留一组上下文直到载具销毁，数量有界。

**F24 CI 的测试清单是写死的 11 个类名。**
`kevin-windows-debug.yml` 用正则只跑这 11 个。之后新增的 `JoystickControlTest`（覆盖解锁状态下的手柄控制）、
`ThrusterMappingIntegrationTest`、`InitialConnectTest`、`DeepSharkUILayoutTest`、`Attitude3DPanelTest`、`GStreamerTest` 都不在里面。
即使修好 F23，新增测试也不会自动进入 CI。

**F25 发布流程不运行任何测试。**
`build-windows-release.ps1:544` 以 `-DQGC_BUILD_TESTING=OFF` 配置 Release。产物的门禁只有文件存在、版本号一致和一次启动自检。
Release 与 Debug 的行为差异（断言、时序）没有任何测试覆盖。

**F26 测试产物和设置残留。**
`ThrusterMappingIntegrationTest` 用 `__FILE__` 反推源码树，把截图写进 `.tmp/codex/output-tool-preview`；
`DeepSharkUILayoutTest`、`DeepSharkVideoResizeTest` 把截图写进当前目录；
`_unknownUidRecordsStayInSession`、`_desktopDialogTabs` 往测试用的设置里写了键没有清理。
另外 `PlatformTest` 在 Windows 上断言 `QT_FORCE_STDERR_LOGGING` 未设置，而本机查看测试输出恰恰需要设置它，带着这个环境变量跑会失败。

**F27 AI 检测对每路相机另开一条 RTSP 连接。**
`run_yolo_to_qgc_auto.py` 用 `cv2.VideoCapture` 自己拉流，不看通道的启用开关，也不看传输方式设置。
开启 AI 后脐带缆上的视频流量和相机的会话数都会翻倍。演示功能，记录备查。

**F22 发布脚本绑定单台机器。**
`build-windows-release.ps1:29-32`、`:156` 写死了 Qt、GStreamer、VS 工具链和构建目录的绝对路径。
换机器或升级依赖版本时要改脚本。脚本里的"覆盖升级契约"是对安装器脚本文本的正则检查，不是实际安装测试，发布记录对此有如实说明。

**F20 诊断信息显示完整 RTSP 地址。**
`DeepSharkStatusPanel.qml:365` 和接收器日志会带出地址里的用户名密码。截图或发日志时注意。

## 读下来确认没问题的部分

- **推进器控制器的状态机**：先回读再禁用、恢复记录先落盘再写参数、用令牌丢弃过期回执、中止时等待在途指令结束、
  功能保存前后各回读一次并拒绝过期值、解锁即中止。这些顺序都对，是这轮读到的代码里最严谨的一块。
- **Motor Test 模式**：QGC 指令队列对 `DO_MOTOR_TEST` 允许重复发送，50 毫秒的连续指令不会被判重复而中断。
- **视频 sink 的帧合并**（`gstqgcqvideosink.cc`）：锁顺序、绑定失效、帧释放都在锁外，逻辑自洽。
- **AI 检测接收**（`AIDetectionReceiver.cc`）：只绑定本机回环，报文大小、数量、来源、时间戳、数值范围都有上限检查。
- **手柄旧配置迁移的油门语义**：新旧代码对"中位为零 / 负推力"的处理公式一致
  （旧 `Joystick.cc:653` 对照新 `Joystick.cc:1158`），轴功能枚举顺序与迁移写入的 0–3 对应。
- **布局切换不会触发视频重连**：四宫格、主辅、全景、全屏只改几何，不更换输出对象。
- **AUV 控制器不向机器人发任何指令**，与"保持仿真定位"的决定一致。
- **视频测试的跳过是编译期条件**，CMake 已为相关用例设置启用 GStreamer 的环境变量，不是空跑。
- **断联监测的测试**用注入时间逐步驱动状态机，断言的是行为而不是文本，质量可以。
- **告警横幅、告警设置页、AI 叠加层**：没有发现问题。
- **升级时改过的 17 个上游测试**：没有发现断言被削弱。改动是三类：把写死的英文期望换成翻译后的字符串（中文环境下才能通过）、
  把空断言换成真断言（`BluetoothWorkerTest` 原来是 `QVERIFY2(true, ...)`）、跟随有意的行为变更（`onPipelineRestart` 不再重置缓存）。
- **定制测试的质量**：以真实协议往返和注入时间为主，断言的是行为。推进器集成测试对回执保留、参数拒绝、备份回读、
  功能保存的互锁都有覆盖。缺的是 F01、F04 的 `No Action`、T01 的心跳中断这几条路径。
- **AUV 控制器**（整文件已读）：只读写本地设置和解析任务文件，不向机器人发任何指令。
- **翻译**：中文源码译文已完成 2838 条（升级前 2181 条），JSON 译文 1524 条；全部已完成条目的占位符与原文一致，没有空译文。
  仍有 1327 + 360 条未翻译，主要是通用无人机功能。

## 声明核对

从 10-06、10-07 的修复记录里抽了 7 条"已修复"回代码核对：

| 声明 | 结果 |
| --- | --- |
| 停止脚本加入新版 Debug 程序名 | 属实，`StopDeepSharkQGC.cmd:26` |
| 安装规则恢复 AI 工具目录 | 属实，`cmake/install/Install.cmake:22-27`（连测试目录一起装进去了） |
| 安装器捕获旧卸载程序返回码 | 属实，`nullsoft_installer.nsi:97` |
| 独立 YOLO 入口识别新 Debug 配置 | 属实；未指定配置时优先读 Debug 配置而不是正式版配置 |
| 当前手柄选择与逐车辆启用状态的旧键迁移 | 属实，`JoystickManagerSettings.cc` |
| 拍摄按钮横向布局、旧 `MNT_` 参数回退 | 属实 |
| main 的 Windows Debug 工作流"YAML 检查通过，远端未运行" | 写的时候属实；之后远端每次都失败，见 F23 |

## 审查做不到、需要实机确认的

按风险排序：

1. 推进器直测：测试进行中拔掉网线再插回，观察推进器是否停转、界面如何提示（T01）；
   并确认 Disabled 输出在收不到新指令时是否保持 PWM（T01、T02 的前提）。
2. 切换模式卡顿的静音对比实验。
3. 三路视频连续运行一小时以上，记录 GPU 专用内存和进程内存曲线（F06）。
4. 拔掉一路相机三分钟再插回，看是否自动恢复（F01）。
5. 机器人解锁状态下重启 QGC，看工具栏上锁按钮和手柄是否可用（F07）。
6. 覆盖安装 1.5.0 → 2.0.0 后的手柄、视频、AI 配置是否保留（发布记录自己写明未验证）。
7. 3D 姿态模型的横滚、俯仰、航向方向与实物一致（v5.1.5 的 HUD 俯仰方向与旧版相反，3D 面板的符号是独立写的）。
8. 右侧预览在 3D 姿态与 RTSP 1 之间切换时的 GPU 占用对比（F21）。

## 下一轮

- 仍未逐行读的：`AuvMissionPanel.qml`、`AIDetectionSettings.qml`、`tools/ai_detection` 的三个脚本、`test-windows-release.ps1`、上游测试里的纯新增用例。
- 对 F04、F07、T01 用 MockLink 写失败用例坐实（需要编译）。
