# 三路视频与 Windows 主窗口缩放稳定性

日期：2026-10-07。修改基于当前 `main`，本轮前提交为 `3c746e8acc4a971e72e28441893c3bb8ce6faabe`。

## 结论与范围

用户报告的触发方式是快速反复拖动 QGC 整个 Windows 主窗口的右下角，三路视频、未开启 AI；1.5.0 安装版与 2.0.0 Debug 都可能永久无响应，需要结束进程。

本轮在当前 2.0.0 Debug 中修复三项从代码和定向回归确认的问题：GUI 视频帧积压、单接收器启动清空多路共享 GPU 资源，以及旧输出的可见性观察者干扰新绑定。新增加压测试没有发生永久无响应，但尚未取得用户现场挂死时的线程栈，因此不能把这三项问题直接等同于已确认的现场死锁根因。

`D:\Develop\MyApp\QGC_for_GRobot-legacy` 仅用于只读对照，1.5.0 未修改。旧版的视频显示路径与当前版不同，当前修复不能直接视为旧版修复。

## 实现

- `gstqgcqvideosink.cc`：稳定输出绑定最多保留一帧待交付视频和一个待执行 GUI 唤醒；新帧替换旧帧，避免 GUI 忙于缩放时队列持续持有解码缓冲。切换输出、停用、caps/segment 变化、flush、stop 和销毁会清理待交付帧并使旧任务失效。GPU 帧释放及 Qt `setVideoFrame()` 均在内部锁外执行。
- `HwBuffers.cc`：`onPipelineRestart()` 保留其他视频仍在使用的进程共享设备和导入缓存，仅重置必要的发现重试状态。设备错误和窗口场景图失效仍保留全局重置路径。测试验证三路逐个启动及连续缩放期间共享 D3D11 包装对象没有被替换。
- `QGCQVideoSinkController.cc`：窗口与输出的观察者归属于当次 controller 绑定。旧 controller 释放但尚未延迟删除、以及删除之后，旧窗口隐藏或旧输出脱离都不能关闭新的显示出口。
- `frames-delivered` 现在统计 GUI 调用 `QVideoSink::setVideoFrame()` 后的交付量；它不是屏幕实际呈现帧数。映射路径统计仍由现有硬件路径遥测维护。
- 合成视频源明确采用 I420，避免测试用 x264 自动选择 Y444。真实摄像头的收流配置未改。

主窗口大小变化沿现有布局与渲染链路处理，未新增因缩放而重连视频的逻辑。此次视频修复未重排正常主页；测试窗口里的三列视频覆盖层仅存在于 `DeepSharkVideoResizeTest`。此前获批的工具栏和手柄页面调整见独立 UI 审计记录。

## 验证

Debug 增量构建成功，最终日志：`.tmp/codex/video-resize-20261007/build-final.log`。

定向 CTest 三组通过，总计 10.98 秒：

- `GStreamerTest`：包含三路突发帧仅交付最新帧、缓冲释放、输出切换、目标/元素销毁、流重置，以及旧可见性观察者回归；同时保留现有 GPU 与接收器测试。
- `DeepSharkVideoControllerTest`。
- `DeepSharkVideoResizeTest`：默认 offscreen 环境中原生缩放用例明确跳过；下表为另行运行的 Windows 原生结果，不能用这项 CTest 的通过代替原生验证。

CTest 日志、JUnit 和结果 JSON 位于 `build-v5.1.5-debug/video-resize-final-*`。

Windows 原生测试使用真实 QGC MainWindow、真实 `VideoTile`、三路本机 RTP/H.264 1280×720、每路 30 FPS、D3D11 RHI 和 threaded render loop。没有启动 AI 检测进程。窗口左上角固定，每次请求在大、小尺寸之间直接跳变（宽高约 60%～100%，普通模式遵守窗口最小尺寸），连续运行 30 秒，并断言实际运行达到所请求的时长。Qt 和 Win32 触发间隔均请求 1 毫秒；[Win32 timer 有系统最小间隔](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer)，实际频率以窗口 geometry 变化统计为准。

| 原生测试方式 | 实际尺寸变化 | 实际变化频率 | GUI 最长间隔 | 窗口渲染帧数 | 各路解码增量 | 各路 GUI 视频交付增量 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 普通主窗口连续调整尺寸 | 397 | 13.22 次/秒 | 111 ms | 992 | 902 / 901 / 901 | 396 / 394 / 398 |
| Windows sizing modal 内程序调整尺寸 | 372 | 12.39 次/秒 | 181 ms | 862 | 901 / 901 / 901 | 304 / 304 / 304 |

实测 QWindow geometry 尺寸范围：普通模式 `1068×599`～`1781×999`，modal 模式 `1063×585`～`1781×999`；每次是大幅跳变。两轮实际压力阶段分别为 30029 / 30030 毫秒。

两轮各 3 项 QTest（含初始化、清理），均 0 失败、0 跳过。modal 测试确认进入、退出各 1 次，实际收到 372 次 `WM_SIZE`，原生 RECT 变化 372 次，没有尺寸调整调用失败。每轮结束后，每路还继续收到至少 10 帧，并取得原生窗口截图。

GUI 与视频最长间隔、实际尺寸变化、解码及交付进展和缩放后恢复均有断言。外部辅助进程设置 90 秒超时，避免 GUI/渲染线程真正挂死时测试无限等待。此次均正常结束，未取得挂死栈。

原生证据位于 `.tmp/codex/video-resize-20261007/`：`threaded-geometry-*`、`threaded-modal-*` 及 `video-resize-threaded.png`、`video-resize-threaded-native-modal.png`。

格式使用项目要求的 clang-format 22.1.5：视频 sink 和新增测试整文件通过；其他改动文件与 HEAD 比较没有新增格式诊断。保留既有整文件诊断，数量和源行比对见 `format-baseline-comparison.json`。九项适用的非 clang-format 检查通过（包括 CMake 格式、lint）；固定等待检查仍被原有 `GStreamerGstQgcTest.cc` 的 `QTest::qWait(150)` 阻挡，此行也存在于 HEAD，未新增固定等待。`git diff --check` 通过。未宣称全量 lint 或整仓 Unit 全部通过，未制作安装包。

## 复测与边界

当前 checkout 的辅助脚本已保留在忽略目录，可执行：

```powershell
.\.tmp\codex\video-resize-20261007\run-native.ps1 -RenderLoop threaded -DurationMs 30000
.\.tmp\codex\video-resize-20261007\run-native.ps1 -RenderLoop threaded -NativeModal -DurationMs 30000
```

持久测试入口为标准 Debug 启动器的 `--unittest:DeepSharkVideoResizeTest --onscreen`，在子进程中设置 `QT_QPA_PLATFORM=windows`、`QT_QUICK_BACKEND=rhi`、`QSG_RHI_BACKEND=d3d11`、`QSG_RENDER_LOOP=threaded`、`QGC_TEST_ENABLE_GSTREAMER=1`、`QGC_TEST_VIDEO_RESIZE_MS=30000`；modal 模式另设 `QGC_TEST_VIDEO_NATIVE_MODAL=1`。日志使用 `QGC_LOG_LEVEL=warning`，测试自身分类诊断仍保留。测试使用独立 unittest QSettings 命名空间，未改正常 Debug 或安装版配置。render loop 环境仅应用于测试，正常启动策略未改变。

尝试投递方向键和目标窗口鼠标消息不能在本机产生真正的原生边框尺寸变化，原强断言正确判失败；该无效驱动已移除，失败证据保留为 `threaded-posted-mouse-not-covered.xml`。最终 modal 测试通过进入实际 Windows 缩放消息泵、再调用自有 HWND 的 `SetWindowPos` 来改变尺寸，**没有模拟真实鼠标拖拽，也没有覆盖真实 `WM_SIZING` 输入行为**。相关机制参考 [Windows sizing modal 文档](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-entersizemove) 与 [SetWindowPos 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)。

真实摄像头协议、码率、硬件解码/驱动行为和用户的实际鼠标拖拽仍需实物复测；未逐帧证明全部使用 GPU 零拷贝。GUI 交付量在强缩放时低于解码量，符合只保留最新帧的策略，不能据此宣称缩放期间保持完整 30 FPS。若实际场景仍永久无响应，应在原进程挂死时读取线程栈，确认阻塞链后继续修复，避免仅根据可能的 GUI/渲染线程等待关系改动 Qt 同步机制。

当前 Debug：`build-v5.1.5-debug/Debug/QGC_KevinJiang_v5_1_5_Debug.exe`，SHA256：`75DCC11BA76687F7B8C8C5E9EA85C305E1020453AD11B8E1A00DF8B9CE80E0D3`。此前 UI 审计中的程序哈希属于此前 UI 验证阶段；本记录给出视频修复后的构建。可通过现有 `StartDeepSharkQGC.cmd` 或 Debug 启动脚本进行实物复测。

改动尚未提交推送；其他 UI 和发布流程工作区 diff 保留，没有混入本轮视频修复的归因。
