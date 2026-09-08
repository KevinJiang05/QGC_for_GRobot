# QGC GRobot 性能基线与优化方向

## 实测摘要

以下为原任务在优化前记录的历史基线，不是本轮改动后的复测结果。本次范围以用户最新要求为准：仅验证 Debug 正常运行，不构建 Release、安装包或进行分发验收。

- 日期：2026-09-08
- 构建：`build-debug-ai\Debug\QGC_KevinJiang.exe`
- 启动方式：`StartDeepSharkQGC.cmd`
- 视频：三路 1920x1080 RTSP，第四路停用
- 冷启动到进程出现：约 0.61 秒
- 冷启动到三路 RTSP 建立：约 10.87 秒
- 稳定工作集：约 1.13-1.17 GB
- 私有内存：约 0.96-1.02 GB
- 线程：约 98-101
- 句柄：约 3,877-4,067
- GPU 本地显存：约 753-754 MB
- CPU：约 60%-70% 单核等效负载

连续约 60 秒采样没有发现工作集、私有内存、线程或句柄持续增长。运行约一小时后的采样也没有显示持续增长证据。

## 已完成的优化

- `FourVideoPanel.qml` 使用四个固定的状态对象，以属性绑定更新各字段；移除了周期刷新和重复的视频事件刷新入口。FPS 改变不再替换整张数组或重建 Repeater 的状态行。
- 停用通道时不再创建对应视频输出 sink；已有 sink 会被脱离并释放。
- AI overlay 已改为按需 Loader，仅在通道有效且用户开启 overlay 时创建。
- 通道停用并确认不在启动/重连后，控制器会延迟释放 receiver，避免保留 GStreamer worker。
- 控制器记录每次启动到首个解码帧的 `elapsedMs`，用于分析首帧等待。
- 重新启动 receiver 时恢复帧率计时器；未启动及已释放的通道不运行该计时器。
- 清除 QML 输出时，启动中或尚未停止的 receiver 保留 sink，避免提前释放视频线程可能仍使用的对象。
- 快速停用再启用时，输出项重新出现也会等待旧启动/停止完成；普通首次启用仍使用原来的延迟启动，不在属性初始化阶段强制重建 sink。
- 消除了 `_ensureReceiver` / `_rebuildSink` 相互调用造成的一次初始化重复创建 sink；旧 receiver 的排队信号通过 QPointer 排除，避免影响重新创建的 receiver。
- 普通启动器不再强制开启 GStreamer 详细日志；`StartDeepSharkQGC.cmd --diagnostics` 显式启用视频、控制器日志和现有 `--log-output`。磁盘日志由应用现有保存目录下的 `CrashLogs/QGCConsole.log` 管理，stdout/stderr 重定向不作为日志已生效的证据。

首帧日志的终点是 sink 首帧通知到达控制器，并非屏幕完成呈现；单个 `elapsedMs` 不能独立拆分 RTSP、解码和显示各阶段耗时。

每次改动只运行直接相关的构建和 `DeepSharkVideoControllerTest`，没有提前执行最终的连续重连和长时间稳定性测试。

## 原任务已验证

- 三路 RTSP 可以同时建立。
- 全部重连后的动态视频 pad 问题已修复并现场验证。
- 当前短时资源曲线稳定。
- GStreamer 视频诊断日志可通过 `--logging:VideoAllLog` 开启。

## 接续任务验证（2026-09-08）

本节早期结果对应已撤回的中间实现，不能直接用于当前代码验收。

- Debug 编译成功；`build-debug-ai/goal-resume-build.log` 保存本轮构建输出。
- `DeepSharkVideoControllerTest`：CTest 1/1 通过，约 9.08 秒，退出码 0。新增覆盖 receiver 延迟销毁、重新启用恢复计时器、停用后启动失败释放，以及状态表随 FPS 变化更新。结果见 `build-debug-ai/goal-resume-test.log`。
- 新测试首次使用普通 QQuickItem 作为 GStreamer 视频控件而崩溃；已改用项目真实 VideoTile 后重跑通过。
- 三台摄像头的 554 端口可连接，仅证明 RTSP 端口可达。
- 19:40 启动 Debug 后，前视和右视有动态画面，左视显示在线但黑屏，第四路停用。约两分钟时工作集 1213 MB、私有内存 1012 MB、110 线程、4116 句柄；单次采样不能用于判定资源趋势或优化收益。
- stdout/stderr 重定向文件为空，因此尚无可引用的本轮首帧时间数据。
- 界面验证被用户物理 Escape 停止；没有确认完成一次全部重连，也未进行最终连续 10 次、断流恢复或 30 分钟验收。

## 回归处理与当前交接

- 用户随后报告 Qt `constWrapper` 断言（`qv4qobjectwrapper.cpp:1511`）以及全部视频无法连接。断言尚无经证实的单一根因，不宣称已修复。
- 一次构建因旧进程占用 EXE 出现 LNK1168，链接失败；该次运行不能代表源代码中的最新改动。
- 已将中间改动保存为 `build-debug-ai/optimization-before-recovery.patch`，并以提交 `2a97fb1ea` 的相关文件完成对照构建和定向测试（CTest 1/1，17.67 秒，退出码 0）。没有重置分支、提交历史或现场资料。
- 按用户最新要求继续优化：在基线之上重新实现固定状态对象、AI 按需加载、受停止完成约束的 receiver/sink 释放和首帧日志；未修改分辨率、帧率、GStreamer latency 或已验证的 pad 修复。
- 增加固定状态行与垃圾回收测试，以及启动中停用、快速重新启用、sink 延迟释放和 receiver 重建测试。当前构建与测试结果以 `build-debug-ai/optimization-build.log`、`build-debug-ai/optimization-test.log` 为准。
- 最终本轮验证：Debug 链接成功，CTest `DeepSharkVideoControllerTest` 1/1 通过（8.25 秒，退出码 0）；真实状态面板在 100 次 FPS 更新与主动垃圾回收中没有新增或删除状态行。`git diff --check` 通过。没有重新执行现场界面测试。
- 用户明确由其手动进行界面和硬件验收；助手不再自动启动或操作现场界面。首帧耗时、资源收益、三路恢复、10 次重连、断流恢复和 30 分钟稳定性仍待用户实测。

## 尚未完成

- 启动策略优化：目前进程启动很快，主要等待来自视频连接和首帧；首帧耗时日志已具备，尚未据此调整策略。
- 用户已反馈视频不再黑屏、画面正常；Qt Debug 断言修正后的现场复测仍待完成。
- 视频 buffer 和 GL 显存的细化分析。
- 停用通道后 receiver/worker 释放的现场资源对照。
- AI overlay、最小化面板、第四路停用的 CPU/GPU 对照。
- 最终统一验收：连续 10 次全部重连、单路断流恢复、30 分钟稳定性。

## 后续方向

1. 先用首帧日志确认是否存在可优化的启动等待。
2. 验证 constWrapper 兼容修正后 Debug 三路实际画面与状态一致，且不再弹断言。
3. 对视频、AI overlay、最小化面板做 CPU/GPU 对照。
4. 只有测量证明必要时，才调整 buffer、latency 或解码策略。
5. 全部优化完成后，再统一执行连续重连和长时间稳定性测试。

## 验收边界

当前定向测试不能代替硬件与长时间验收。用户已确认画面恢复，但断言修正的现场验证、资源对照、断流恢复和连续重连尚未关闭，Debug 稳定性目标尚未完成；Release 和安装包不属于本次范围。

## constWrapper 断言的复现与兼容处理

- 用户确认终止进程后，使用本项目 `QGCCorePlugin` 对象构造两个 QML 引擎，在一个引擎中访问普通对象包装、另一个引擎中读取 const 对象属性，再执行垃圾回收。修正前自动测试稳定触发 `qv4qobjectwrapper.cpp:1511 ASSERT: "constWrapper"`，CTest 退出码 8，进程异常码 `0xc0000409`。证据：`build-debug-ai/const-wrapper-repro-test.log`。
- Qt 官方修复 `b5a6abf05bbfeddabf95cd4ee4f8bd5973b92b84` 说明：QObject 上的 `hasConstWrapper` 标记不能代表某个 QML 引擎中仍存在对应包装。来源：https://github.com/qt/qtdeclarative/commit/b5a6abf05bbfeddabf95cd4ee4f8bd5973b92b84 。这证实了同一断言的可复现机制，但没有取得现场进程调用栈，不能据此断言现场也采用了相同的多引擎路径。
- 保留 Qt 6.8.3，针对项目的 6 处 `const QObject*` QML 属性改为普通 QObject 派生类指针，并同步 getter 类型；仍不提供 WRITE，保留原有 CONSTANT / NOTIFY。涉及 options、flyView、customMapItems、adsbVehicles、assignableActions、series。没有关闭 Debug 断言、修改 Qt DLL、改变视频 pipeline 或降低视频质量。
- 新增 `_coreOptionsSurviveGarbageCollection` 回归用例，验证跨引擎读取核心配置和 100 次主动垃圾回收。其余 3 处同类接口随构建检查，不宣称完成对应设备/图表功能的现场验收。
- 修正后 Debug 链接成功，CTest `DeepSharkVideoControllerTest` 1/1 通过（7.26 秒，退出码 0），包括原有视频资源生命周期、状态行稳定性与新增 GC 用例；`git diff --check` 通过。日志：`build-debug-ai/const-wrapper-fix-build.log`、`build-debug-ai/const-wrapper-fix-test.log`。本轮未启动现场界面，仍由用户手动验收。
