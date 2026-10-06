# 云台输出通道分配修复记录

日期：2026-10-06。分支：`main`。

## 问题和原因

`APMGimbalInstance.qml` 的输出通道处理存在两处遗漏：选择新通道时只写新通道，
未清除该轴的旧分配；禁用时在清除第一个匹配项后提前返回，留下其他重复分配。
俯仰从通道 5 切到 6 后，两者均为 `FUNCTION=7`，随后禁用仍有一个通道启用。

升级前基线 `db9c9159631a271457a7d6c395deb50e2cf3c304` 中，
`APMCameraSubComponent.qml` 的 `setRCFunction` 会先清除该功能的全部旧分配，
再设置选定通道。此前迁移测试检查了参数家族、页面实例化和读取时不改写 Fact，
没有覆盖实际输出通道的切换、禁用操作，因此未发现这个行为差异。

## 修改范围

- 输出通道处理先确认目标 Fact 存在，再清除其他通道上同一轴的功能分配，最后设置目标。
- 选择禁用时清除全部匹配项；保留其他功能的通道分配。
- 沿用现有 `_servoFact`、`_servoParameterName` 和通道扫描，不新增参数存储。
- 旧 `MNT_` 页面兼容 `RCx_FUNCTION` 和 `SERVOx_FUNCTION`；新版使用 SERVO 参数。
- 仅修改该页面的输出通道处理及控件测试标识，补充现有兼容测试类；没有修改 RC 输入功能。

## 回归证据

测试实例化真实 `APMGimbalInstance` 和输出选择控件，以内存中的真实 Fact 作为参数。
通过控件的 `activated` 信号运行实际处理逻辑，没有另写一份算法进行测试。

新增 9 组：旧 MNT/RC、旧 MNT/SERVO、新 MNT/SERVO，分别覆盖俯仰 7、偏航 6、横滚 8。
每组验证通道 5 → 6、旧通道清零、新通道赋值、重建页面仍显示通道 6；再注入重复分配，
禁用后检查全部匹配项已清零，重建页面仍显示禁用，并确认其他通道的功能 25 未被改写。

修复前，已校正控件查找的同组测试有 9 项失败，断言为通道 5 仍保留轴功能值。
修复后 17/17 通过（6 组原有参数兼容、9 组新增输出操作、2 项生命周期），无失败或跳过。
证据位于 `.tmp/codex/gimbal-channel-fix/`：

- `before-control-APMGimbalCompatibilityTest.xml`：修复前 9 项失败。
- `after-APMGimbalCompatibilityTest.xml`、`after.log`、`after-result.json`：修复后通过。
- 更早的 `before-*` 测试包含夹具查找/字体问题，不作为通道缺陷的复现证据。

Windows Debug 增量编译和链接通过；限定文件的 clang-format、合并冲突、
禁止 QTest::ignoreMessage、禁止固定 qWait 检查及 `git diff --check` 通过。
记录文档的 markdownlint 通过。单文件 `qmllint --bare` 未通过，修复前后均有
40 条相同诊断（忽略路径和行号后比较），包括原有重复 id 与导入解析告警；
日志为 `qmllint-before.log` 和 `qmllint.log`。没有为此扩大修改原有页面结构。

最终执行 `ctest -C Debug -L Unit -LE 'Flaky|Network' --parallel 1 --timeout 90`，
202 个测试类/入口中 199 个通过、3 个失败，无超时，用时 404.48 秒。
`APMGimbalCompatibilityTest` 在此轮同样通过。失败为 `QGCMAVLinkTest`、
`SysStatusSensorInfoTest`、`SigningControllerTest`；这些测试类没有在本次修改中改动，
现有 CTest 输出未包含具体失败断言，原因尚未核实，不标记为已知旧问题。
依照本次输出分配修复的限定范围，没有扩大处理其他模块。
证据为 `build-v5.1.5-debug/gimbal-output-unit-tests.log`、
`gimbal-output-unit-junit.xml` 和 `gimbal-output-unit-test-result.json`（退出码 8）。
专项通过不代表全仓库检查通过。

当前 Debug 程序：`build-v5.1.5-debug/Debug/QGC_KevinJiang_v5_1_5_Debug.exe`。
SHA-256：`909debded4a3691b9e00adce6e65f431f02caf85ab3ca78322e618eb9a40ca8a`。

## 验证边界

没有连接或操作真实飞控、云台或其他设备，没有制作安装包、提交或推送。
回归验证参数和页面行为，不证明真实设备的参数 ACK、持久化和机械输出已验收。
此前迁移修复记录中的 Debug 哈希属于当时产物，本记录对应本次增量编译后的产物。
