# QGC-GRobot upstream patch inventory

> 快照日期：2026-09-02  
> 上游基线：`v5.0.8`（`b05e82c3e41157396c9d90e91500161f066987cd`）  
> 当前分支：`deepshark/v5.0.8`  
> 用途：升级 QGroundControl 前快速判断冲突面、保留理由和最小回归范围。  
> 最近更新：2026-10-05，U04 AI 核心集成已迁入 `custom/`；DeepShark 不再使用 QGC 原生视频，原生视频上的 AI 挂载点已删除（见第 2 节末尾和 U04）。

## 1. 使用规则

- 新产品功能默认放入 `custom/`；只有缺少可用扩展点、必须改变 QGC 核心行为，或修复通用上游缺陷时才修改上游目录。
- 修改现有上游文件时，必须在下表登记：目的、不能留在 `custom/` 的原因、最小验证和退出策略。
- 同一目的涉及多个文件时按模块归并，避免逐文件重复描述；第 4 节给出精确文件覆盖。
- 每次同步新上游基线时先更新本文件，再处理冲突；不能仅凭“能编译”判断补丁仍然需要。
- 本清单只管理升级冲突面，不取代发布 runbook、测试报告或硬件验收记录。

## 2. 当前规模与结论

以当前工作树相对 `v5.0.8` 复核：

- 53 个在基线中已存在的文件被修改，其中 `src/` 36 个、测试注册 2 个、构建/发布基础设施 12 个、翻译 2 个、根 README 1 个；
- 另有 5 个 AI 文件新增在上游拥有的 `src/FlightDisplay/` 中；
- 当前工作树还有 9 个有意新增但尚未跟踪的 `test/DeepShark/` 测试文件，它们不计入上述 Git tracked diff；
- 因此此前审计中的“51 个上游修改文件”已经过时，应以本快照的 53 个既有文件修改为准。

2026-10-05 复核（U04 迁移时）：

- 迁移前 HEAD 相对 `v5.0.8` 有 71 个既有文件被修改，多于本快照的 53 个；09-02 之后新增的修改尚未逐项归入下表模块（未核实归属）；
- U04 迁移后为 66 个：`src/FlightDisplay/CMakeLists.txt`、`src/QGCApplication.cc`、`src/Settings/Video.SettingsGroup.json`、`src/Settings/VideoSettings.cc/.h` 已恢复为上游原样；
- `src/` 下改动从 55 个文件 +2176 行降至 45 个文件 +719 行，`src/` 中不再有 DeepShark 新增文件。
- 同日确认 DeepShark 不再使用 QGC 原生视频：`FlyViewVideo.qml`、`FlightDisplayViewVideo.qml` 恢复上游原样，`FlyView.qml` 去掉 PiP AI 叠加；既有文件修改降为 65 个，`src/` 为 44 个文件（含同日 GStreamer receiver 修复提交）。

优先收口顺序：

1. ~~**U04 AI 核心集成**~~：2026-10-05 已迁入 `custom/`，仅剩视频设置页 1 个挂载点；
2. **U07 默认 TCP 策略**：产品默认值不应长期驻留通用 `LinkManager`；
3. **U09 参数页入口**：推进器工具入口可用 custom QML override，通用按钮刷新修复则应独立成可上游补丁；
4. 其余通用兼容或底层行为补丁保持小而可测，不为追求零 diff 进行高风险重构。

## 3. 模块清单

| ID | 模块与当前目的 | 为什么当前触及上游 | 最小验证 | 退出/上游策略 |
|---|---|---|---|---|
| U01 | **DeepShark CI**：限定 `deepshark/**` 和 custom/AI/test 触发面，构建 Debug/Release，并运行 Python 与 Qt 定向测试 | GitHub workflow 属于仓库基础设施，不能放进运行时 `custom/` | workflow YAML 可解析；远端 Debug/Release 首跑；DeepShark CTest 与 Python 测试执行 | 长期保留仓库级 workflow；路径随模块迁移同步更新，不向 QGC 上游提交产品分支规则 |
| U02 | **跨平台命名、版本和安装包**：产物改为 `QGC_KevinJiang`，支持版本覆盖、图标、桌面快捷方式、卸载信息和升级安装 | CPack、NSIS、AppImage 入口位于上游构建基础设施；现有 custom override 尚不能覆盖全部打包行为 | `tools/release/build-windows-release.ps1` 完整 `succeeded`；安装、覆盖升级、卸载、哈希；Linux/macOS CI 冒烟 | 产品品牌逻辑不向上游提交；优先把可参数化项收进 `CustomOverrides.cmake`，只保留通用打包 hook |
| U03 | **品牌说明、仓库忽略和中文翻译**：二次开发声明、产物/模型忽略、中文资源和关闭自动 Crowdin 拉取 | README、Help 页面、翻译和 ignore 是仓库级资源；Help 品牌块目前直接改上游页面 | About/Help 实际显示；翻译生成；`git status` 不纳入本地产物；手动 Crowdin workflow 可运行 | Help 品牌块可迁 custom override；翻译保留差异但避免混入未使用字符串；不向上游提交产品品牌文案 |
| U04 | **AI 叠加与设置的挂载点**：`AIDetectionManager/Receiver`、单例注册（`DeepSharkPlugin::init`，`DeepShark 1.0` 模块）、叠加组件、AI 设置组件和叠加开关（`AIDetection/OverlayEnabled`，首次读取兼容旧 `Video/yoloOverlay`）均已位于 `custom/`，叠加只在 DeepShark 四路 `VideoTile` 中显示；上游只剩视频设置页中的 `AIDetectionSettings {}` 一行。原生视频/PiP 叠加已随停用 QGC 原生视频删除（只有 `source_id` 为空的单源开发脚本/样例发送器数据会显示在那里） | 视频设置页属于上游页面，custom 目前没有向其中插入子项的扩展点 | AI Python 测试；`AIDetectionReceiverTest`；DeepShark QML 同步加载；Windows Debug 构建；设置页显示 AI 区块；旧叠加开关值迁移；真实 1/2/4 路叠加、启停和退出长稳 | 设置页挂载点随 ConnectionAlertSettings 一起保留；若将来改为独立 DeepShark 设置页，可一并移出 |
| U05 | **FlyView、遥测条和相机控件布局**：地图遮挡由 FlyView 单向持有、原生确认层置顶、遥测转置、相机按钮横向布局及空数据保护 | 一部分是 DeepShark 布局需求，一部分是通用控件健壮性；当前改在核心 QML 以影响原生布局 | FlyView qrc 同步加载；地图/视频主视图切换；确认滑块可见；不同 DPI/窗口宽度；遥测配置编辑；相机拍照/录像 | 产品布局优先迁 custom override；`InstrumentValue*` 空数据保护可整理为独立上游修复；删除无行为意义的纯空白差异 |
| U06 | **视频源策略和 MPEG-TS 低延迟**：不让 ArduSub/自动流覆盖“禁用视频”选择，低延迟模式将 `tsdemux latency` 设为 0 | 行为位于 Vehicle、VideoManager 和 GStreamer receiver 核心链路，custom 当前没有等价稳定 hook | 视频 source 设置迁移；禁用状态保持；MPEG-TS/RTSP 播放、断线和恢复；端到端延迟对比；非低延迟模式回归 | “尊重禁用”与低延迟设置具备通用价值，可拆成小补丁向上游提交；若新版本已修复则删除本地补丁。DeepShark 已停用 QGC 原生视频，`Vehicle.cc`/`VideoManager.cc` 的“尊重禁用”正是保持原生视频关闭的机制，不能当作原生视频残留删除；GStreamer receiver 同时服务 DeepShark 四路视频 |
| U07 | **GRobot 默认 TCP 与 NoProxy**：首次创建 `192.168.1.200:4019` 自动连接，保留既有用户端点；TCP 明确绕过系统代理 | 默认连接创建发生在 `LinkManager` 加载阶段；NoProxy 属于底层 socket 行为 | 无配置首次启动；已有同名/同端点/用户自定义配置；重启持久化；代理环境连接；真实飞控断连恢复 | 默认端点应迁至 custom plugin/首次运行配置 owner；NoProxy 是否普适需单独评估，必要时做成配置而非全局强制 |
| U08 | **ArduPilot/ArduSub 参数兼容**：新旧 mount、灯光输出、`ARMING_SKIPCHK`、可选 failsafe 参数、4.8 metadata 和窄类型 enum 容错 | 这些页面和 metadata 解析器属于 APM 核心插件；custom 复制整页会制造更大的漂移 | 用旧/新参数集合加载相关 QML；缺失参数不报警/不写错；4.8 metadata 可见；真机检查 arming、灯光、云台和 failsafe | 多数属于通用兼容修复，优先拆分并向 QGC 上游提交；新基线已有等价实现时删除，不长期维护整页 fork |
| U09 | **摇杆按钮和推进器映射入口**：参数下载完成后刷新按钮 Fact，避免错误 action 映射；参数页增加 DeepShark 工具入口 | 摇杆刷新是上游通用缺陷；推进器入口是产品功能但当前直接插入核心 ParameterEditor | 参数未就绪/就绪切换；固件/QGC action 切换；shift action；DeepShark qrc 加载；推进器 controller 定向测试和真机 ACK/恢复 | 摇杆刷新整理成独立上游补丁；推进器入口迁为 custom `ParameterEditor.qml` override 或 custom 导航入口 |
| U10 | **GPS 驱动依赖钉住**：固定到兼容 QGC v5.0.8 构造函数的 PX4-GPSDrivers revision | 依赖版本由上游 `src/GPS/CMakeLists.txt` 管理，custom 无法安全覆盖 CPM 声明 | 清空相关 CPM 缓存后 configure；Windows Debug/Release 编译；GPS 模块链接 | 升级 QGC 时先尝试新上游 pin；若 API 已适配则删除本补丁，不把旧 revision 永久带入新基线 |
| U11 | **DeepShark Qt 测试注册**：把 AI UDP、视频竞态、AUV Plan 和推进器 controller 纳入 CTest | 项目测试树和全局注册表属于上游目录，但测试本身是 custom 功能的必要回归面 | `ctest -N` 能发现；四个定向测试 4/4；CI Debug 执行相同正则 | 测试源继续集中在 `test/DeepShark/`；只保留最小的两个注册文件改动，升级时优先复用新上游测试自动发现机制 |

## 4. 精确文件覆盖

以下按主要责任归组；同一文件只列一次，即使它同时服务多个目的。

### U01 DeepShark CI（1）

- `.github/workflows/custom.yml`

### U02 构建、发布与安装（10）

- `.github/workflows/linux.yml`
- `.github/workflows/macos.yml`
- `.github/workflows/windows.yml`
- `cmake/CreateAppImage.cmake`
- `cmake/CreateCPackNSIS.cmake`
- `cmake/CreateWinInstaller.cmake`
- `cmake/Git.cmake`
- `cmake/Install.cmake`
- `deploy/windows/nullsoft_installer.nsi`
- `.gitignore`

### U03 品牌、文档与翻译（5）

- `.github/workflows/crowdin_docs_download.yml`
- `README.md`
- `src/UI/AppSettings/HelpSettings.qml`
- `translations/qgc_json_zh_CN.ts`
- `translations/qgc_source_zh_CN.ts`

### U04 AI 挂载点（1；2026-10-05 前为 13，其中 5 个新增文件已移入 `custom/src/`，7 个文件已恢复上游原样或只剩其他模块改动）

- `src/UI/AppSettings/VideoSettings.qml`（`AIDetectionSettings {}`；文件中另有 ConnectionAlertSettings 和解码优先级可见性改动）

### U05 FlyView、遥测与相机布局（8；2026-10-05 起 `FlightDisplayViewVideo.qml` 的纯空白差异已恢复，`FlyView.qml` 归入本模块）

- `src/FlightDisplay/FlyView.qml`
- `src/FlightDisplay/FlyViewBottomRightRowLayout.qml`
- `src/FlightDisplay/FlyViewWidgetLayer.qml`
- `src/FlightDisplay/TelemetryValuesBar.qml`
- `src/FlightMap/Widgets/PhotoVideoControl.qml`
- `src/QmlControls/HorizontalFactValueGrid.qml`
- `src/QmlControls/InstrumentValueLabel.qml`
- `src/QmlControls/InstrumentValueValue.qml`

### U06 视频核心行为（3）

- `src/Vehicle/Vehicle.cc`
- `src/VideoManager/VideoManager.cc`
- `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.cc`

### U07 连接策略（3）

- `src/Comms/LinkManager.cc`
- `src/Comms/LinkManager.h`
- `src/Comms/TCPLink.cc`

### U08 APM/ArduSub 参数兼容（9）

- `src/AutoPilotPlugins/APM/APMCameraComponentSummary.qml`
- `src/AutoPilotPlugins/APM/APMCameraSubComponent.qml`
- `src/AutoPilotPlugins/APM/APMLightsComponent.qml`
- `src/AutoPilotPlugins/APM/APMSafetyComponentSub.qml`
- `src/AutoPilotPlugins/APM/APMSafetyComponentSummarySub.qml`
- `src/FirmwarePlugin/APM/APMParameterMetaData.cc`
- `src/FirmwarePlugin/APM/APMResources.qrc`
- `src/FirmwarePlugin/APM/ArduPilot-Parameter-Repository`
- `src/UI/toolbar/APMMainStatusIndicatorContentItem.qml`

### U09 摇杆、参数页与推进器入口（3）

- `src/QmlControls/ParameterEditor.qml`
- `src/Vehicle/VehicleSetup/JoystickConfigButtons.qml`
- `src/Vehicle/VehicleSetup/SetupView.qml`

### U10 GPS 依赖（1）

- `src/GPS/CMakeLists.txt`

### U11 测试注册与测试文件（11）

- `test/CMakeLists.txt`
- `test/UnitTestList.cc`
- `test/DeepShark/AIDetectionReceiverTest.cc`（当前未跟踪）
- `test/DeepShark/AIDetectionReceiverTest.h`（当前未跟踪）
- `test/DeepShark/CMakeLists.txt`（当前未跟踪）
- `test/DeepShark/DeepSharkAuvControllerTest.cc`（当前未跟踪）
- `test/DeepShark/DeepSharkAuvControllerTest.h`（当前未跟踪）
- `test/DeepShark/DeepSharkVideoControllerTest.cc`（当前未跟踪）
- `test/DeepShark/DeepSharkVideoControllerTest.h`（当前未跟踪）
- `test/DeepShark/ThrusterDirectControlControllerTest.cc`（当前未跟踪）
- `test/DeepShark/ThrusterDirectControlControllerTest.h`（当前未跟踪）

53 个基线既有修改文件的覆盖校验：U01 1 + U02 10 + U03 5 + U04 8 + U05 8 + U06 3 + U07 3 + U08 9 + U09 3 + U10 1 + U11 2 = 53。U04 的 5 个新增 `src` 文件和 U11 的 9 个当前未跟踪测试文件另计。以上为 09-02 快照口径；2026-10-05 起 U04 为 1 个既有文件、无新增 `src` 文件。

## 5. 升级时的最小操作顺序

1. 记录新 QGC tag/commit，重新生成 `git diff --name-status <new-base> --`；
2. 先处理 U10 依赖 pin，确保可以完成干净 configure；
3. 再处理 U08、U06、U07 的核心行为与固件兼容；
4. 处理 U04、U05、U09 的 custom 边界和 QML 冲突，优先删除已被上游覆盖的补丁；
5. 最后恢复 U01/U02/U03/U11 的 CI、品牌、发布和测试契约；
6. 先跑模块定向测试，再按发布 runbook 生成完整报告；没有 `succeeded` 报告和安装/升级验证时，不建议分发。

复核命令：

```powershell
git diff --name-status v5.0.8 --
git diff --name-only --diff-filter=M v5.0.8 --
git ls-files --others --exclude-standard src test
```
