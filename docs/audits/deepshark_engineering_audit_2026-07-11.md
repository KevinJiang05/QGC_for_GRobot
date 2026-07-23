# DeepShark QGC 二次开发只读工程审计

- 审计日期：2026-07-11（Asia/Shanghai）
- 基线：QGroundControl v5.0.8，分支 `deepshark/v5.0.8`
- 当前 HEAD：`f509574c8`（tag `v1.1.1`）
- 审计方式：只读代码与 Git 检查、现有 Debug 增量构建检查、12 秒轻量启动存活验证
- 审计边界：`custom/`、DeepShark 启停/RTSP/AI 工具、DeepShark 直接修改的少量 QGC 集成点；上游代码仅用于核对接口契约

## 结论摘要

当前实现已经形成了清晰的自定义插件边界，四路视频、布局、持久化配置、独立启停、状态面板和重连链路也不是演示壳子；视频接收器和 sink 的基本所有权处理总体合理。因此，它适合作为继续工程化的原型和比赛主控台候选。

但当前版本还不能评价为“比赛主控级可靠”。最主要原因不是界面，而是两个运行边界尚未闭合：

1. 推进器映射工具的实机测试没有把一次操作绑定到固定 Vehicle 身份，断链或活动载具切换时，停止命令和参数恢复没有可靠落点。
2. RTSP watchdog 已经存在，但其策略被改成所有通道无限重试，且启停、URL 变更、QML watchdog 和 `VideoReceiver` 异步回调共同驱动状态，没有单一状态机所有者。

建议先修复 Critical/High 项并补定向测试，再继续扩展 watchdog。3D 姿态实际上已经加入；它是只读显示，安全耦合较低，可以保留，但不应继续扩展到控制功能，且应在真实飞控和四路视频并发下完成轴向、时延和 GPU 验证。

## 证据类别说明

- **代码确认**：仅凭当前代码和接口契约即可确认设计缺口或错误路径存在。
- **构建/运行验证**：本轮实际执行后确认。
- **现场待验证**：必须有真实四路 RTSP、飞控、交换机/链路和比赛电脑负载才能定论。

## 审计发现（按严重程度）

### Critical — DS-C01 实机推进器测试未锁定目标 Vehicle，断链/切换时可能丢失停止与恢复，或作用到另一载具

**结论类别：代码确认。现场仅用于验证后果，不用于确认问题是否存在。**

证据：

- `custom/src/DeepShark/ThrusterMappingTool.qml:26` 将 `activeVehicle` 动态绑定到全局当前载具。
- `:487-503`、`:520-523`、`:554-587` 的 PWM、Motor Test、停止和参数恢复每次都重新读取当前 `activeVehicle`，没有保存启动测试时的 Vehicle 身份。
- `:630-648` 的计时器在测试期间继续运行；`:672-730` 只监听当前 Vehicle 的命令结果和 armed 变化，没有处理 `activeVehicle` 被替换或变为 null。
- `:130-138` 只在弹窗正常关闭时尽力停止；进程崩溃、链路丢失或对象切换不在该保障内。

触发条件：Motor Test 或直接 SERVO 输出进行中，飞控断链、活动载具切换、Vehicle 对象销毁/替换，或 QGC 异常退出。

实际影响：

- 原载具可能收不到 0 输出/中位 PWM。
- 临时写成 Disabled 的 `SERVOx_FUNCTION` 可能无法恢复。
- 若活动载具切到另一台，后续脉冲、停止或恢复动作可能发给错误载具。
- 这是物理执行链风险，严重度高于普通 UI 或视频故障。

最小修复方向：启动测试时持有并校验固定 Vehicle（至少 system id、对象生命周期和 primary link）；活动载具变化或链路丢失立即在本地封锁后续命令并进入“恢复未确认”状态；所有停止/参数恢复只允许发给原 Vehicle；在重新允许测试前要求人工确认飞控侧状态。

### High — DS-H01 直接 SERVO 测试对参数写入/恢复没有事务确认

**结论类别：代码确认；是否在特定飞控/网络上出现残留必须现场验证。**

证据：

- `ThrusterMappingTool.qml:164-171` 通过 Fact `rawValue` 异步写参数后立即返回成功，没有等待参数回读或 ACK。
- `:467-475` 把 `SERVOx_FUNCTION` 写为 0 后只等待固定 700 ms。
- `:501-506` 随后直接发送 PWM；`:520-523` 结束时写中位值并立即恢复参数，同样不验证回读。
- `:684-686` 只处理 DO_SET_SERVO 被拒绝的文本状态，不会回滚/锁死流程；也没有把“恢复已确认”和“仅已发出恢复请求”区分开。

触发条件：高延迟、丢包、参数写队列拥塞、飞控拒绝写入、断链或在 700 ms 窗口内切换状态。

实际影响：界面可能报告“已恢复”，但飞控参数尚未恢复或根本未恢复，形成持久配置失真；DO_SET_SERVO 也可能在通道仍被功能占用时执行或被拒绝。

最小修复方向：将“备份→禁用→回读确认→输出→中位→恢复→回读确认”实现为显式状态机；任何一步失败都停止后续动作并显示不可忽略的恢复告警；备份需带 Vehicle/固件/参数哈希身份，不应跨载具复用。

### High — DS-H02 启动脚本优先选择 Debug 构建，并永久追加高详细度日志

**结论类别：代码确认；现场性能和日志增长量待长时验证。**

证据：

- `StartDeepSharkQGC.cmd:13-20` 在 Debug 和 Release 同时存在时总是选择 Debug。
- `:8-10,23` 注入 Debug CRT 路径。
- `:31` 开启 GStreamer/video receiver debug 日志；`:43` 对 `%TEMP%` 日志使用永久追加，没有轮转、截断或磁盘上限。
- 本轮实际发现 Debug 可执行文件约 114 MB，Release 约 41 MB。

触发条件：比赛电脑保留开发 Debug 目录并使用该脚本启动；多路 RTSP 长时间抖动或重连。

实际影响：现场实际运行版本可能与发布/验收版本不一致；Debug 构建和高频日志增加 CPU、I/O、磁盘增长及诊断噪声，长场次可能放大视频抖动。

最小修复方向：默认只启动已验收 Release/安装版本，Debug 必须显式参数开启；启动时打印版本、commit 和配置路径；日志按大小/场次轮转并设总量上限；启动成功应以进程存活/主窗口或健康信号确认，而不是只确认 `cmd.exe` 已创建。

### High — DS-H03 已有 RTSP watchdog 的重试策略从“仅本地源无限”漂移为“四路全部无限”

**结论类别：代码确认；资源退化程度和恢复成功率待真实四路 RTSP 验证。**

证据：

- `custom/src/DeepShark/VideoTile.qml:44` 当前固定 `maxAutoRetries: 0`；`:191-193` 将 0 解释为无限。
- Git 历史 `fbaa0960a` 原本只对 `rtsp://127.0.0.1:8554/deepshark` 取消上限；当前行由 `707b487e0` 改成全局 0，策略和提交意图不再一致。
- `:206-229` 每次失败均停止并重新建链；四个 tile 独立执行。
- `custom/src/DeepSharkVideoController.cc:202-206` 只有 `receiver->started()` 为真才调用 stop；启动尚未完成时，QML 的“停止后重连”不能真正取消当前启动。
- `:76-109` 的 URL/autoStart `singleShot` 与 QML 的 `restartTimer`、`reconnectDelay`、watchdog 回调共同触发 start，缺少代次 token 来淘汰旧请求。

触发条件：摄像头未插、URL 不可达、RTSP 握手卡住、频繁保存 URL、网络反复上下线或解码只串流不出帧。

实际影响：通道会无限重建 GStreamer 管线；多个异步启动请求可能交叠为 INVALID_STATE/额外重连，造成 CPU、线程、日志和网络负载长期抖动。通道开关能人工止损，但默认四路 Enabled，不能替代有界退避。

最小修复方向：由 C++ 单一状态机拥有 start/stop/reconnect；使用 generation token 丢弃旧定时任务和旧回调；区分本地聚合流与远端摄像头策略；采用指数退避、抖动和熔断，保留明确的人工恢复入口；QML 仅展示状态和发命令。

### Medium — DS-M01 DeepShark 地图隐藏逻辑会破坏 QGC 原有 `enabled` 绑定

**结论类别：代码确认。**

证据：

- 上游集成点 `src/FlightDisplay/FlyView.qml:102` 把 `mapControl.enabled` 绑定到 `!viewer3DWindow.isOpen`。
- `custom/src/FlyViewCustomLayer.qml:65-75` 通过 JavaScript 赋值 `mapControl.enabled = !hideMap`；QML 命令式赋值会移除原绑定。

触发条件：DeepShark 层至少执行一次可见性同步，之后再打开 QGC 原生 3D Viewer。

实际影响：地图不再跟随原生 3D Viewer 状态禁用，可能出现重叠输入、隐藏层仍接收事件或后续状态恢复错误。

最小修复方向：不要写入由上游绑定拥有的属性；在父级组合最终可见/可用条件，或增加独立 DeepShark gate 并保持原绑定表达式。

### Medium — DS-M02 AI 检测框没有新鲜度/来源上限，旧结果可无限停留，UDP 输入可放大 UI 负载

**结论类别：代码确认；比赛网络暴露程度待现场确认。**

证据：

- `src/FlightDisplay/AIDetectionReceiver.cc:90` 绑定 `AnyIPv4` 并允许地址复用。
- `:111-152` 接收任意 JSON 数据报，没有发送方校验、数据报/检测数上限或时间戳检查。
- `:146-182` 按任意 `source_id` 永久保存在 `_detectionsBySource`，仅收到同 source 的新包或无 source 包/禁用时清理。
- `AIDetectionVideoOverlay.qml:39-56` 每次变化都重建对应 Repeater 模型。

触发条件：AI 进程崩溃但不再发包、源移除、错误程序持续发送不同 source id、大检测数组或比赛网段上有非预期 UDP 数据。

实际影响：画面继续显示过期框，造成状态失真；source 哈希和 Repeater 负载可持续增长，影响主线程和渲染。

最小修复方向：每源保存接收时间并在 0.5–2 秒无更新后清空；限制允许的 source id、每包字节数和检测数量；默认只绑定 localhost（如确需远端则显式配置/过滤）；在 UI 标出数据年龄。

### Medium — DS-M03 未提交的默认 TCP Link 逻辑会在加载配置时写持久设置，且判定规则过宽

**结论类别：代码确认；本轮未读取工作区外用户配置，是否已写入当前用户设置未知。**

证据：

- 工作树存在未提交修改：`src/Comms/LinkManager.cc/.h`。
- `LinkManager.cc:382` 在加载配置列表时调用 `_addGRobotDefaultTCPLinkIfNeeded()`。
- 新函数只要发现任意非动态持久连接就放弃创建 GRobot 默认连接；没有按目标 host/port 或连接类型判定。
- 创建后立即 `saveLinkConfigurationList()`，把“加载”路径变成持久化写路径。

触发条件：首次启动无持久连接，或已有无关持久连接。

实际影响：前者会静默改用户配置；后者又可能导致预期默认连接缺失。当前 `autoConnect=false`，因此它也不等于比赛链路自动恢复方案。

最小修复方向：先明确这是安装初始化、首次运行向导还是运行时默认；按稳定 ID/host/port 精确判重；不要在普通 load 路径隐式保存；加入迁移版本和用户可见确认。

### Medium — DS-M04 状态面板把 Vehicle 永久显示为 `Unknown`

**结论类别：代码确认。**

证据：`FlyViewCustomLayer.qml:176` 固定传入 `vehicleStatus: "Unknown"`，而 `DeepSharkStatusPanel.qml:116` 将它作为正式系统状态显示。

实际影响：比赛操作员无法从该面板判断飞控连接真实性；“状态面板”语义强于它实际提供的信息。

最小修复方向：绑定 activeVehicle、链路心跳年龄和 armed/模式；未知、未连接、数据过期必须区分，且所有状态带更新时间。

### Medium — DS-M05 DeepShark 没有自动化回归测试，QML 静态检查也未形成可用目标

**结论类别：构建/运行验证。**

- `ctest --test-dir build-debug-ai -N` 返回 `Total Tests: 0`。
- 仓库搜索未发现 DeepShark controller/settings/state-machine 定向测试。
- 手工 `qmllint` 因 Custom/DeepShark 资源模块缺少离线 import/qmltypes 元数据产生大量不可判定警告，虽退出 0，但不能证明 QML 正确。

实际影响：重连、通道开关、参数恢复、Vehicle 切换等高风险状态只能靠人工回归；后续加 watchdog 或 3D 容易把已有行为悄然改坏。

最小修复方向：先覆盖纯状态机和设置层；为 QML 建可解析模块或最小 qmltestrunner harness；至少把“禁用不重连、旧 generation 不启动、断链停止执行、参数恢复确认、AI 过期清除”变为自动测试。

### Low — DS-L01 仓库跟踪了未被资源系统使用的旧 `.qmlc`

**结论类别：代码确认。**

`custom/src/DeepShark/FourVideoPanel.qmlc`、`VideoTile.qmlc`、`custom/src/FlyViewCustomLayer.qmlc` 被 Git 跟踪，时间为 2026-04；`custom/custom.qrc` 只引用 `.qml`。它们当前不参与运行，但容易让维护者误判实际加载内容，也增加版本噪声。

### Low — DS-L02 启动脚本硬编码本机工具链和仓库绝对路径

**结论类别：代码确认。**

`StartDeepSharkQGC.cmd:4-10` 固定 `D:\Develop\...`。作为开发脚本可接受，但不适合作为比赛发布入口；换机、安装目录变化或工具链升级会直接失效。

## 已确认的正向工程事实

以下不是“未发现问题”的泛化判断，而是本轮有代码证据支持的优点：

- 自定义范围集中在 Custom Plugin、资源覆盖层和少量明确集成点，没有侵入式改造整个 QGC。
- 每个 `VideoTile` 独立拥有 `DeepSharkVideoController`；receiver 以 controller 为 QObject parent，析构时断开回调、删除 receiver、释放 sink。
- GStreamer frame probe 使用原子计数，并正确释放 clock、pad parent 和 pad 引用。
- 通道 Enabled 与 URL 分离持久化；禁用通道会阻止 start/reconnect/reconnect-all，状态面板也能区分 Disabled。
- QML 销毁时停止三个重连/watchdog timer 并请求 receiver stop。
- AUV 任务工作区当前实机模式主要是遥测展示；调试运行、返航、上浮和“急停”按钮被 debugMode 限制，没有在该面板内伪装成已实现的实机任务控制。
- 3D 姿态当前只读取 activeVehicle 的 roll/pitch/heading，不发送飞控命令。

## 本轮构建与轻量运行验证

### 已验证

- `cmake --build build-debug-ai --config Debug --target QGC_KevinJiang`：成功，Ninja 报告 `no work to do`；现有 exe 时间晚于本轮涉及的源码，说明当前增量依赖图认为产物最新。
- 隐藏启动 `build-debug-ai/Debug/QGC_KevinJiang.exe`：12 秒后进程仍存活，没有启动即崩溃；随后本轮终止该进程。
- 启动前后工作树未新增修改，仍只有原有 `LinkManager.cc/.h` 两个未提交文件。

### 不能据此宣称已验证

- 这不是全量 clean rebuild，也没有 Release 重建。
- 隐藏启动没有真实 RTSP、飞控和现场交换机，不能验证画面、重连或姿态数据。
- 本轮没有可运行的 DeepShark 自动测试；应用存活 12 秒不等于资源释放、长时稳定或优雅退出已通过。

## 必须等待真实设备/现场网络的验证项

1. 四路 RTSP 同时运行至少 2–4 小时：记录 CPU/GPU/内存、线程数、句柄数、日志增长、FPS、端到端延迟。
2. 每路执行拔线、断电、URL 不可达、RTSP server restart、只有 streaming 无 decoded frame、交换机抖动；确认退避、熔断、人工恢复和禁用通道都符合预期。
3. 反复切换 grid/mainAux/panorama/fullscreen/map hidden/最小化，并观察 sink、decoder 和显存是否累积。
4. 安全台架上验证推进器测试：断链、活动载具切换、拒绝 ACK、参数写超时、QGC 异常退出后，检查实际 PWM 和全部 `SERVOx_FUNCTION`。
5. 真实 ArduSub 验证 3D 的 roll/pitch/heading 正负号、坐标系、heading 0/360 跳变、遥测过期显示。
6. 四路视频 + 3D MSAA + AI overlay 并发，验证比赛电脑 GPU/渲染线程余量。
7. AI 进程退出、停止发包和源下线后，确认旧检测框能按期限消失；验证比赛网络是否必须开放非 localhost UDP。

## 对用户问题的直接回答

### 当前架构总体是否可靠？

**结构方向可靠，运行边界尚未达到比赛主控级。** Custom Plugin 分层、四路 controller 隔离、持久化和 UI 组织是可继续演进的；但物理执行安全、重连单一真源、状态新鲜度和自动测试仍不足。

### 是否应先修复问题再继续开发？

**是。** 至少先修 DS-C01、DS-H01、DS-H02、DS-H03，再进行新的控制或恢复功能开发。AI 若比赛不启用可后置，但必须默认关闭或明确标为非可信辅助信息。

### 是否适合现在加入 watchdog？

**不适合再叠加一个 watchdog。** 当前已经有 QML watchdog，应先把它收敛成 C++ 单一状态机、有界退避、generation token、熔断和可测试的状态模型。否则新增 watchdog 会成为第二个/第三个并发控制者。

### 是否适合现在加入 3D 姿态？

**3D 姿态已经存在，作为只读功能可以保留，不建议现在扩大。** 先验证轴向、遥测过期和四路视频并发性能；在 Critical/High 项解决前，不要把 3D 面板扩展成实机控制入口。

## 推荐后续执行顺序

1. 冻结现场发布入口：Release 优先、版本可见、日志有上限。
2. 修复推进器工具 Vehicle 身份锁定、断链封锁和参数事务恢复；编写安全台架测试清单。
3. 将 RTSP 启停/重连/watchdog 收敛为 C++ 单一状态机，加入 generation token、有界退避和熔断。
4. 为上述两条状态机补 Qt/C++ 定向测试和最小 QML 集成测试。
5. 修复地图 enabled 绑定、Vehicle 状态真实性和 AI 数据过期/输入上限。
6. 做真实四路 RTSP soak test 与故障注入；测量而非主观判断性能。
7. 做真实飞控姿态轴向/过期验证和推进器安全台架验证。
8. 达到验收门槛后，再决定扩展 3D、AI 或更高级 watchdog/服务编排。

## 建议的比赛前最低验收门槛

- 四路流连续 2 小时无崩溃、无持续资源增长；单路故障不拖垮其他三路和飞控链路。
- 所有自动重连有退避和上限；操作员可一键禁用并得到真实状态。
- 飞控断链/Vehicle 切换时，任何推进器测试立即在本地停止发新命令并显示恢复未确认。
- Release 构建和现场启动脚本指向同一已记录版本；日志有大小上限。
- 3D 姿态在三轴正负、0/360 和断链过期场景全部通过。
- Critical/High 定向测试进入日常构建，现场测试结果有可复查记录。

