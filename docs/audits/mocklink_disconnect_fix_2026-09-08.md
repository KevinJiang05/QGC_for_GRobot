# MockLink 断开时崩溃修复

开始：2026-09-08；后续验证：2026-09-09。

## 问题与根因

此前 `VehicleLinkManagerTest::_simpleLinkTest` 曾在 `MockLink::_paramRequestListWorker` → `mavlink_finalize_message_buffer` 中访问异常。本轮修改前单次运行通过，说明不能依靠一次偶发通过排除此问题。

`MockLinkWorker` 原先位于独立 QThread，却直接调用属于链接主线程的 MockLink 方法。定时发送、参数索引更新与主线程的收包/断开同时访问模拟器状态。`disconnected` 信号会让 LinkManager 立即释放 MAVLink 通道，后台仍可能继续打包；此外，断开前排队的输入回调可能在释放通道后才执行。

## 修复

- 保留现有 worker 和定时器，将它们作为 MockLink 的同线程子对象；取消独立线程。
- 通道分配并连接后启动定时器，断开前停止定时器及任务响应计时器。
- `_writeBytesQueued` 在已断开时直接返回，避免使用已释放的通道解析输入。
- 辅助通道释放后也置为无效值，与主通道保持一致，避免重复释放。

不修改真实串口/UDP 链路或飞控失联门限。

## 第二个晚到回调问题

扩大回归时，参数、FTP、断联预警通过，但链路移除用例又在 `_handleCameraInfoRetry` 崩溃。这次是摄像头请求将 `CameraStruct*` 裸指针保存在 Vehicle 的异步回调中；摄像头管理器销毁后，Vehicle 的命令失败/超时回调仍可能到达。

两种摄像头请求回调现在使用由 Vehicle 持有的请求上下文，内部通过 QPointer 弱引用 CameraStruct。回复到达后释放请求上下文；如果摄像头对象已销毁，直接结束，不再重试或访问它。Vehicle 销毁时也会清理尚未完成的上下文。

新增摄像头回归：发出请求，销毁摄像头管理器，再注入拒绝回复，覆盖 REQUEST_MESSAGE 与旧 CAMERA_INFORMATION 命令两条路径。

## 验证与测试时序

新增 `_mockTrafficAndDisconnectUseOneThread`：检查实际发送回调的线程，故意将参数请求排入队列后立刻断开，确认不再发送数据且车辆正常移除。

完整初始化包含 731 个主组件参数及其他连接步骤。本机诊断中完成时间通常约 3–6 秒，但重复运行中也出现超过 10 秒的情况，原测试的 3 秒上限过紧。初始化测试保留完成状态断言，兼容已经完成的信号，并与现有 UnitTest 连接辅助方法统一为 30 秒上限；高延迟链路回执改为等待实际异步信号，而不是假定主链切换瞬间已处理完命令。通信丢失时限与测试断言不变。MockLink 的 2 ms 参数发送定时器显式使用 PreciseTimer。

诊断运行 `VehicleLinkManagerTest`：8 passed、0 failed、0 skipped，约 37.5 秒。明细保留在 `build-debug-ai/mocklink-qt-detail.txt`；临时 Qt 日志输出代码已移除。

本轮重新编译了 MockLink 使用方与测试对象，避免此前构建目录漏记头文件依赖留下旧对象布局。正式构建和相关回归结果见 `build-debug-ai/mocklink-final-build.log`、`build-debug-ai/mocklink-final-test.log`。

上述 `mocklink-final-test.log` 保留了发现第二个摄像头问题时的结果：预警 3.42 秒、参数 74.24 秒、FTP 3.15 秒通过，链路用例在摄像头回调崩溃，不能把该阶段称为全通过。

摄像头修复后的结果以 `build-debug-ai/mocklink-camera-build.log`、`build-debug-ai/mocklink-camera-test.log` 为准：摄像头和链路 2/2 通过，耗时分别为 3.01 秒、55.29 秒。追加重复验证记录在 `build-debug-ai/mocklink-repeat-test.log`。

最终验证（2026-09-09）：Debug 编译成功；`mocklink-complete-test.log` 中 QGCCameraManagerTest 连续两次通过（3.19、2.46 秒），VehicleLinkManagerTest 连续两次通过（39.59、39.81 秒），CTest 退出码 0。此前 repeat/precise 日志保留了初始化等待超时的中间结果，不代表最终状态。临时诊断代码已移除；本轮未执行发布打包。
