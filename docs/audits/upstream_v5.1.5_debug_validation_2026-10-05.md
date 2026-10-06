# QGroundControl v5.1.5 Debug 候选验证记录

日期：2026-10-05。目标是交付供真实设备测试的 Windows Debug 候选，不是 Release 发布或实机验收。

此文保留当日构建与测试的历史结果。后续中文、设置和发布迁移缺口及修复，
见 [2026-10-06 修复记录](upstream_v5.1.5_migration_repairs_2026-10-06.md)；
本页旧 EXE 哈希及测试覆盖不代表后续修复产物，也不证明完整界面迁移。

## 当前结论

最终源码的 Windows Debug 构建成功；本次修复后的 Unit 测试 **201/201 通过，0 失败、0 超时**。
9 个重点回归及参数通信、MockLink 签名回归通过。真实 MainWindow 已加载 DeepShark 四路视频、状态面板及连接告警，完成两轮关闭重建，并通过两次独立进程启动。
此前 11 项失败/超时的原因、修复和复测证据见下文。AI Python 桥接另有 5 项测试通过。
按用户确认的范围，保留官方 v5.1.5 底座，只修复已确认的功能问题和本次改动涉及的运行风险。
本次范围的原配置静态检查为 10 项通过、XML 规则无匹配文件而跳过；47 个 C++ 文件的改动区间格式检查通过。
**标准全量 lint 仍未通过**：工具依赖安装问题已解决，检查实际执行后报告上游及历史文件的格式、规范问题和工具默认构建路径限制，结果已记录，没有将限定检查当作全量通过。
真实摄像头、飞控、手柄、推进器和长时间运行仍待用户验收。

启动方式及实测清单见 [Debug 使用说明](../debug/windows-v5.1.5-debug.md)。

## 源码与产物身份

| 项目 | 记录 |
| --- | --- |
| 升级前检查点 | `db9c9159631a271457a7d6c395deb50e2cf3c304`，升级前已推送；备份 tag `backup/pre-qgc-v5.1.5` 已推送 |
| 官方旧底座 | `v5.0.8`，`e0816c957602789200ae5ba0af45217f0f2f1db4` |
| 官方新底座 | `v5.1.5`，`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67` |
| 工作分支 | `main`；由原 `deepshark/v5.0.8` 重命名，本地与 GitHub 默认分支已同步，没有创建分支或工作树 |
| 合并方式 | 官方标签的完整 Git 合并，全部冲突已解决；未整目录覆盖 `src` |
| 合并提交 | `6b044f606de525d7f978d4e64f5bcd26f112ab46`，官方合并与定制适配已提交并推送 |
| 构建目录 | `D:\Develop\QGC_for_GRobot\build-v5.1.5-debug` |
| 程序 | `Debug\QGC_KevinJiang_v5_1_5_Debug.exe` |
| 程序 SHA256 | `33a74e82312eeb775be364f4149003d71d5998344e5d908a74982411293598db` |
| 显示版本 | `1.5.0 Debug (QGroundControl v5.1.5)` |
| 普通启动应用名 | `QGC_KevinJiang_v5_1_5_Debug Daily`，组织名 `KevinJiang` |
| 配置隔离 | 候选使用独立应用身份；不自动复制或迁移正式版设置、校准 |

以上测试在合并提交前的最终功能源码上执行。随后提交、分支重命名及仓库治理没有更改这些已验证的功能源码；启动入口另经启动验证，程序 SHA256 已复核一致，未因文档更新重新构建或重跑全量测试。

本次未调用 Claude，未构建 Release、制作安装包、执行 `cmake --install` 或替换正式安装。

## 依赖记录

| 依赖 | 实际版本及位置 |
| --- | --- |
| Qt | 6.11.1，`D:\Develop\envs\Qt\6.11.1\msvc2022_64`，含 `qttasktree` |
| GStreamer | 1.28.4 开发 SDK，`D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64` |
| CMake | 4.1.0，`C:\Program Files\CMake\bin` |
| MSVC | VS 2022 Build Tools，14.44.35207，沿用 `D:\Develop\Toolchains\VS2022BuildTools` |
| 构建 Python | 项目 `.venv`，Python 3.10.11；Ninja 亦从该环境运行 |
| CPM 源码缓存 | `D:\Develop\envs\qgc-cpm-cache` |
| ArduPilotParams | 本次实际缓存提交 `f07078bbb46d45aacc3a8ec24916e0f9b12195ac`；上游配置使用浮动 `main` |
| AI Python | 现有 `D:\Develop\envs\yolo\Scripts\python.exe`，Python 3.10.11 |
| AI 包 | Ultralytics 8.4.48、PyTorch 2.11.0+cpu；CUDA 不可用，本次没有安装或升级 AI 包 |
| Vale 依赖构建工具 | LLVM-MinGW 20260922（Clang 23.1.2），`D:\Develop\envs\tools\llvm-mingw-20260922-ucrt-x86_64`；仅临时进程使用 |
| Vale 文档检查 | 官方 3.15.1 Windows x64，`D:\Develop\envs\tools\Vale\3.15.1` |

GStreamer 官方安装器 SHA256：`1a745d67225e43394a4a5db929c97397cb56e74b1c38bb77c6ded4b037d3c040`。
Vale [官方同版本 Windows 包](https://github.com/vale-cli/vale/releases/tag/v3.15.1) SHA256：`3395fca0ddfb10a9b6caa28e091d5df709b1d6b6579afb7dece852cad89b94f3`，与 GitHub 发布元数据一致。只解压到共享目录，未修改全局环境。
LLVM-MinGW [官方发布包](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922) SHA256：`e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666`，与发布元数据一致；只解压到共享目录，以进程内 `CC`/`CXX` 支持 Vale/tree-sitter 的 CGo 构建，未修改全局环境。
使用 portable/devel 模式安装到上述共享目录；旧 SDK 和旧构建保留。启动脚本仅设置进程环境，含 SDK 的 `lib\libproxy` 路径。
中文 MSVC 的 `/showIncludes` 前缀曾被 CMake 错误解码；只修正本构建目录内的编译器检测元数据，并验证 Ninja 已记录头文件依赖。新建构建目录时须复查，未修改系统工具或全局环境。

## 相对官方 v5.1.5 的核心差异

以下分组覆盖当前 `git diff v5.1.5 -- src` 的 36 个文件。定制控制器、面板和设置仍由现有 `custom` 目录负责。

| 文件或模块 | 保留原因与新版适配 |
| --- | --- |
| `AppSettings/HelpSettings.qml` | 保留定制来源、维护信息，底座说明改为 v5.1.5 |
| `AppSettings/pages/CommLinks.SettingsUI.json`、`Video.SettingsUI.json` | 接入新版设置页面生成流程，保留连接告警、AI 设置及始终可选的解码器 |
| `AutoPilotPlugins/APM/APMFailsafesComponentSummarySub.qml`、`APMFlightSafetyComponentSub.qml`、`APMFlightSafetyComponentSummarySub.qml`、`APMLightsComponent.qml` | 对 ArduSub 可缺省参数使用可空 Facts；保留默认解锁检查掩码说明，以及无 `BRD_PWM_COUNT` 时按实际 SERVO 参数显示灯控通道 |
| `FirmwarePlugin/APM/APMMainStatusIndicator.qml` | 缺失 `FS_GCS_ENABLE`、`FS_GCS_TIMEOUT`、`FS_OPTIONS` 时不访问空 Fact、不显示无效控制 |
| `Camera/QGCCameraManager.cc`、`.h` | 异步相机请求上下文随 Vehicle 存活，弱引用相机管理器，防止管理器销毁后的晚到回调访问释放对象 |
| `Comms/LinkManager.cc`、`.h` | 默认 TCP `192.168.1.200:4019`；已有名称或端点配置优先，不覆盖用户设置 |
| `Comms/TCPLink.cc` | 控制链路使用 `NoProxy` |
| `Comms/MockLink/MockLink.cc`、`.h`、`MockLinkWorker.cc` | Worker 与 Link 共用所属线程，断开时同步停定时器；保留新版 MAVLink v2、签名和视频服务实现；将断言换成通道、请求目标和参数边界检查，失败时安全返回，清理支持重复调用 |
| `FlyView/FlyView.qml`、`FlyViewWidgetLayer.qml` | 在新版 FlyView/mapControl 结构中接入定制遮挡状态，四路内容遮挡地图时停止地图交互并隐藏比例尺 |
| `FlyView/FlyViewBottomRightRowLayout.qml`、`TelemetryValuesBar.qml`、`QmlControls/HorizontalFactValueGrid.qml`、`InstrumentValueLabel.qml`、`InstrumentValueValue.qml` | 保留底部遥测转置布局，补上对象可空及列表边界判断，避免启动/销毁过程中的无效访问 |
| `QmlControls/ParameterEditor.qml` | 从新版参数页打开现有推进器映射工具 |
| `Toolbar/FlyViewToolBar.qml` | 在新版工具栏接入现有连接告警 |
| `Settings/JoystickSettings.cc` | 将当前应用存储中的旧 `Joysticks` 配置迁移为新版 V2；已有 V2 优先，旧键不删除，无效校准不标记为已校准 |
| `Vehicle/Vehicle.cc`、`.h` | 主动断开信号供告警区分人为操作；移除 ArduSub 自动将已禁用视频改为 UDP 的行为；对解锁配置和 FRAME_CONFIG 查询增加参数管理器、参数存在性与 Fact 空值检查 |
| `Vehicle/VehicleSetup/JoystickComponentButtons.qml` | 恢复 ArduSub 普通/Shift 固件按钮 Facts，继续使用新版行和原参数所有者 |
| `VideoManager/VideoManager.cc` | 尊重禁用视频配置，不由自动相机流改写 |
| `VideoManager/VideoReceiver/GStreamer/GstSourceFactory.cc`、`GstVideoReceiver.cc`、`.h`、`VideoReceiver.h` | 沿用新版接收器和显示后端；保留空 URI 停止、视频 pad 选择/重复链接保护、解码输出尺寸及跨线程状态修复，低延迟模式禁用抖动缓冲并调整 MPEG-TS demux 延迟；EOS probe 对无效上下文和事件安全返回 |
| `Utilities/FileSystem/QGCFileHelper.cc` | 优先识别本地绝对路径，避免将 Windows 盘符当作 URL scheme，修复归档、GeoTag 等共用调用链 |
| `Utilities/Compression/QGClibarchive.cc` | 写入归档条目时明确使用 UTF-8 路径，修复 Windows 多语言文件名提取 |

`custom` 的插件退出使用新版 `destroyQmlApplicationEngine`，先移除 URL 拦截器再销毁引擎；视频输出接入新版 `VideoBackend` 和 Qt Multimedia `VideoOutput`。
定制接收器关闭上游自动重连，由原有 VideoTile/连接监控统一管理重连，避免两个所有者同时重试。
过期 FlightDisplay 资源和旧工具栏覆盖已迁移到新版 FlyView/Toolbar 结构。

以下旧 const 包装补丁已撤销，当前文件与官方 v5.1.5 一致：

- `src/ADSB/ADSBVehicleManager.h`
- `src/API/QGCCorePlugin.cc`、`src/API/QGCCorePlugin.h`、`src/API/QGCOptions.h`
- `src/AnalyzeView/MAVLinkInspector/MAVLinkMessageField.h`
- `src/Joystick/Joystick.h`

撤销依据为 Qt 6.11.1 的多引擎/垃圾回收验证通过，并有完整 MainWindow 关闭重建测试；不继续携带仅针对旧 Qt 的包装。
上游已经修复的 GStreamer sink parent 引用释放直接采用官方实现。旧 ArduPilot 参数子模块改用新版 CPM 下载，旧本地目录保留且不参与新版构建。

## 已执行验证

最终复测均使用上述 SHA256 对应的程序。产物和日志统一在 `build-v5.1.5-debug` 中；AI Python 证据沿用未改动桥接代码的前次验证。

| 检查 | 实际结果与证据 |
| --- | --- |
| Windows Debug 构建 | 最终编译和链接退出码 0，`build.log`；配置含 `QGC_DEBUG_CANDIDATE=ON`、`QGC_BUILD_TESTING=ON`、`QGC_BUILD_INSTALLER=OFF` |
| CTest 发现 | 274 项，`test-discovery.json`；重点回归、新 UI 测试与参数/签名集成测试均实际注册 |
| Unit 测试 | 201/201 个 CTest 类通过，0 失败、0 超时；2026-10-05 23:23:05，耗时 372.44 秒；`unit-final-tests.log`、`unit-final-junit.xml`、`unit-final-test-result.json`；仍使用 `-L Unit -LE 'Flaky\|Network' --parallel 1` |
| 最终重点回归 | 9/9 个 CTest 类通过；2026-10-05 23:24:15；`focused-final-tests.log`、`focused-final-junit.xml`、`focused-final-test-result.json` |
| 参数及签名集成回归 | `ParameterManagerTest`、`MockLinkSigningTest` 2/2 通过；2026-10-05 23:16:05；`mock-final-tests.log`、`mock-final-junit.xml`、`mock-final-test-result.json` |
| 完整定制界面 | `DeepSharkUILayoutTest` 通过，Windows 原生窗口、显式 `--onscreen`、开启 GStreamer；2026-10-05 23:24:18 完成两轮打开/关闭/销毁/重建，每轮确认 4 个视频控制器和工具栏告警并等待渲染帧；`native-ui-result.json`、`native-ui-results-DeepSharkUILayoutTest.xml`；0 失败、0 跳过 |
| 界面截图 | `deepshark-debug-ui-1.png`、`deepshark-debug-ui-2.png`；最终截图已视觉检查，四路面板、状态区和底部遥测实际显示 |
| 独立进程启动/重启 | 两次退出码均为 0；2026-10-05 23:25:35、2026-10-05 23:25:45；`boot-final-1.log`、`boot-final-2.log` 及对应结果 JSON；简单启动模式使用 PID 隔离测试设置 |
| AI Python 桥接 | 5 项通过，`ai-python-tests.log`；现有环境导入检查通过，`ai-runtime-check.log`；桥接和 AI 环境未在本次失败修复中改动 |
| 工作区检查 | 无未解决合并路径；相对 v5.1.5 的本次源码/交付文件空白检查通过，`diff-check-upgrade-scope.log`；整个官方合并的已有空白问题保留；启动脚本 PowerShell 语法通过 |
| 本次范围的原配置静态检查 | 10 项通过，1 项 XML 规则无匹配文件而跳过；未修改 `.pre-commit-config.yaml` 或关闭规则；84 个实际文件与结果在 `final-scoped-lint-files.json`、`final-scoped-lint-results.json`，日志为 `final-scoped-lint-*.log`；跳过规则不作为验证通过 |
| 本次 C++ 与定制 CMake 格式 | 47 个 C++ 文件的改动区间检查通过，`upgrade-cpp-style.json`；定制 CMake 与 DeepShark 测试 CMake 完整格式检查沿用 `custom-cmake-format.log`；新增 UI 测试完整格式通过，`new-ui-test-format.log` |
| 交付文档格式 | 最终两篇 Debug 文档 markdownlint 通过；Vale 前次按上游 `--no-exit` 执行时报告技术词拼写等问题，不能将退出码 0 称为无报告项 |
| 标准全量静态检查 | 工具依赖已成功构建；`pre-commit run --all-files` 实际执行，退出码 1，未通过；`lint-followup.log`、`lint-baseline-summary.json`，限制见下文 |
| 自动格式改动收尾 | 撤回检查工具产生的 98 个无关文件的格式/空白改动，保留本次功能修复；`lint-noise-cleanup.json`。未删除文件，也未覆盖用户的原有修改 |

9 项重点测试：`QGCCameraManagerTest`、`GStreamerTest`、`AIDetectionReceiverTest`、`DeepSharkAuvControllerTest`、`DeepSharkConnectionMonitorTest`、`DeepSharkVideoControllerTest`、`ThrusterDirectControlControllerTest`、`JoystickTest`、`VehicleLinkManagerTest`。
覆盖相机晚到回调、MockLink 断开、旧手柄配置、合成视频流开始/停止/URI 清空/重用和 QML 资源加载。
视频测试初始化 GStreamer 并实际执行接收器断言，未因关闭后端而跳过。
8 个重点测试类的全部子用例无跳过；`GStreamerTest` 的 102 个子用例中有 10 个跳过，原因为当前 Windows 构建没有 DMABuf/VA-API、Vulkan upload 或视频方向元数据接口。
这些平台/可选功能未被验证，真实 GPU 路径仍需设备测试。逐例明细和修复相关类计数在 `regression-case-summary.json`。
201/201 表示所选 CTest 类全部通过，不表示所有平台的全部子用例或未选 Integration/Network/Flaky 类均已验收。

## 全量 lint 的保留问题

用户确认的范围是“尽量与官方保持一致，修复确定的问题，其他问题记录并按需单独打补丁”。
因此保留完整官方 v5.1.5 底座；全量工具对未定制官方文件、历史文档、固件文本等报告的格式或规范问题没有批量清洗。
`lint-baseline-summary.json` 记录 42 项 hook 的实际状态，以及三个带断言的文件与官方标签文本一致的校验：`VideoBackend.cc`、`QmlObjectTreeModel.cc`、`QGeoTiledMappingManagerEngineQGC.cpp`。
全量 `Q_ASSERT` 检查仍未通过；本次涉及的 MockLink/GStreamer 断言已换为实际防护检查，限定文件检查通过。
`Vehicle.cc` 的两处空值风险已经修复，限定空值检查通过，不再作为待修复项。

全量检查还报告 C++/CMake 格式、Python/YAML/Markdown 规范、历史文件空白、脚本权限或体积等问题。
这些报告不能全部归为功能故障，也没有将每一条都确认成上游缺陷。对检查工具本身的限制另行区分：

- 标准 clang-tidy hook 默认要求 `build/compile_commands.json`，本任务实际数据库在 `build-v5.1.5-debug/compile_commands.json`，该 hook 未完成有效分析。
- QML 和 clazy hook 显示 Passed，但工具按默认分支与当前提交的三点差异收集文件，且不接受 pre-commit 传入的文件名；执行检查时升级尚未提交，不能据此宣称已完整检查。后续提交不改变原检查的覆盖范围，真实界面、QML 加载与原生窗口验证独立记录。
- 根 CMake 自动格式器曾把目录名 `test` 改成 `TEST`，该自动修改已撤回；未为了格式结果改变构建语义。
- 定制 CMake 的 `CUSTOM_SOURCES`、`CUSTOM_INCLUDE_DIRECTORIES` 是公开扩展接口，未为变量命名规则改变接口。
- 上游配置中的 Vale 使用 `--no-exit`；显示 Passed 不保证没有拼写/文风报告。

本次范围的原配置检查包括合并标记、JSON、文件名大小写、分类日志、禁止 `Q_ASSERT`、禁止 `QTest::ignoreMessage`、禁止固定 `qWait`、`QT_TRANSLATE_NOOP`、交付文档格式和 Vehicle 空值检查，10 项均通过；XML 规则按文件类型筛选，无匹配文件而跳过。
C++ 格式单独按改动区间检查，以保留未修改上游代码的风格。官方检查配置保持原样；没有宣称完整 CI 或 Release 验证全绿。
整个合并的 `git diff --cached --check` 及官方 `v5.0.8..v5.1.5` 对照仍报告原有文档、源码和翻译空白，日志在 `diff-check-*.log`、`diff-check-results.json`；限定的本次改动检查独立记录。

界面复核曾因漏传上游 `--onscreen` 参数进入 offscreen 模式，因字体环境缺失触发 Qt 断言。
已按 Windows 原生窗口及系统字体目录重新执行；没有屏蔽断言或停用视频后端。

## Goal 完成标准对照

| 标准 | 当前证据 |
| --- | --- |
| Windows Debug 构建成功 | 最终编译和链接退出码 0，实际 EXE 及上述 SHA256 |
| 定制界面实际加载、关闭和重启 | Windows 原生 MainWindow 测试两轮创建/关闭/销毁，截图已检查；独立进程简单启动验证 |
| 相关离线回归通过且实际执行 | 9 个重点 CTest 类无失败，关键定制类无跳过，逐例 XML 明细确认晚到回调和 MockLink 断开等路径 |
| 可运行路径、启动方式、结果、简明实测清单 | 实际程序存在，公共启动脚本已用于最终程序验证；本记录及 Debug 使用说明 |
| 真实设备与长时间运行待用户验收 | 下一节和实测清单明确列出；没有将离线结果称为实机通过 |

本记录的完成范围是可交接的 Debug 候选；Unit 测试已修复并通过，标准全量 lint 的未通过结果仍属于已记录限制，不能作为完整 CI 或 Release 已验证的证据。

## 此前 11 项失败或超时的处理

初次扩展测试为 190/201 通过、10 项失败、1 项超时，原证据仍保留在 `unit-tests.log`、`unit-junit.xml`。
下表记录确认的原因和实际处理；最终 `unit-final-junit.xml` 确认全部 11 个类均通过。

| 测试 | 原因与处理 |
| --- | --- |
| `GeoTagControllerTest` | 本地 Windows 盘符被 QUrl 当作 scheme，改变了驱动器字母；QGCFileHelper 优先识别本地绝对路径，测试保留原路径断言 |
| `QGCArchiveModelTest` | 同一 Windows 路径问题导致本地归档被当作远程 URL 拒绝；修复共用 FileHelper 后原归档测试通过 |
| `QGCCompressionTest` | QFile::link 在 Windows 创建快捷方式而非目录符号链接；测试改为临时目录 junction 并实际检查目标文件。归档 Unicode 条目未明确指定 UTF-8；修复 libarchive 写入路径 API，多语言 manifest 的提取/存在断言通过 |
| `LinkConfigurationTest` | 有效语法的 `.invalid` 域名在本机得到地址，旧测试依赖外部解析行为；改用包含空 DNS 标签的 `drone..invalid`，仍验证主机名保留及地址为空 |
| `BluetoothWorkerTest` | 两个离线测试误调用真实适配器并使用无意义成功断言；复用现有虚函数，以测试替身隔离硬件，实际验证重复连接仅调用一次、断开复位重连次数与主动断开标记；工厂/定时器等原用例保留。真实蓝牙未验收 |
| `PX4ParameterMetaDataTest` | 测试预期缺失 name，而解析器会先跳过无 name 节点；修正为确实触发失败的缺失 type 并匹配当前翻译后的实际警告，保留无效元数据断言 |
| `QGCMAVLinkTest` | 本机中文返回“未知”，旧测试写死英文；预期使用相同翻译上下文，保留 Unknown 枚举行为断言 |
| `QGCMapPolygonTest` | KML 错误文本受界面语言影响；测试使用对应翻译并检查失败与坏文件信息，保留几何行为断言 |
| `JsonHelperTest` | 错误提示受语言影响；使用相同翻译模板，仍验证错误的预期/实际类型和值 |
| `JsonResourceAuditTest` | 每种翻译都重复读取/解析全部大型参数 JSON，达到既有 60 秒超时；initTestCase 一次验证全部资源语法并收集带 fileType 的资源，每种翻译仍调用实际 typed loader。最终 26 个子用例 0 失败、0 跳过，14.454 秒；未延长原 60 秒上限 |
| `PlatformTest` | 上游只在 Unix 非 Android 设置 stderr 环境变量，旧测试错误要求 Windows 相同；按实际平台契约断言，保留 offscreen 平台验证 |

FileHelper 新增盘符路径回归，MockLink 防护由现有参数管理、签名及链路断开回归验证；没有关闭严格日志检查或移除数据正确性断言。
本次未修改系统语言、DNS、蓝牙状态、符号链接权限或正式配置来消除失败。

额外运行 `ParameterManagerTest` 时，初次有 4 个子用例失败，证据保留在 `mock-initial-junit.xml`、`parameter-timing-ParameterManagerTest.xml`。
时序日志确认：组件信息请求在约 1.855 秒开始，参数请求在约 3.692 秒才开始，而测试的 1 秒进度等待已于 3.691 秒结束。
修复仅在该测试类：进度与完成信号同时监听，在原有加载完成期限内验证正进度、最终归零、参数就绪和缺失参数状态，错误提示匹配实际翻译；没有修改生产超时、重试或最终加载期限。
最终该类全部子用例通过，MockLink 签名、启用/禁用循环及签名后的任务传输回归也通过。

## 实测交接边界

候选第一次启动使用新配置，需在界面填写真实视频端点，并配置现有 AI Python、桥接脚本和本地模型。
旧手柄迁移只处理当前应用内的旧键；不会主动打开正式配置，也不会将旧云台轴转换成语义不同的新版辅助轴。
真实手柄的轴方向、油门零位和固件按钮须在安全状态逐项核对。
CPU 版 AI 环境能导入不等于检测吞吐量、GPU 或多摄像头性能已通过。

下一步按 [简明实测清单](../debug/windows-v5.1.5-debug.md#简明实测清单) 验收真实设备及长时间运行；稳定后另行安排 Release 发布。
