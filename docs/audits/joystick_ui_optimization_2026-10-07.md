# 2.0.0 Debug 主页面与手柄 UI 优化验收

日期：2026-10-07。基于当前本机 `main` 工作区（提交基线 `3c746e8ac`）实施用户已审核的方案。

## 完成内容

- 主页面顶部顺序调整为：模式 → 解锁／上锁 → 心跳检查。普通解锁／上锁直接显示在工具栏，沿用现有 `QGCDelayButton` 的 500 ms 长按确认、参数就绪与健康检查条件。状态抽屉保留诊断和显式选择后的强制解锁入口。
- 手柄页面改为常规、按钮分配、校准、高级四页；设备选择、启用控制和校准状态固定在顶部。未连接设备或载具时显示对应提示。无轴设备默认显示按钮分配，未校准设备默认显示校准。
- 常规页集中显示轴响应、摇杆示意和按键响应；水下载具轴标识为前后、横移、转向、升沉。
- 按钮分配使用完整列表（沿用协议支持的最多 64 个按钮），并排显示地面站动作、飞控动作、Shift 组合动作和重复选项；按下按钮时高亮对应行。
- 校准页保留原有校准状态机、死区和原始通道监视。校准期间禁用切页与设备选择。高级页集中保留油门、指数曲线、更新频率、死区及扩展轴设置。
- 地面站动作继续保存稳定动作 ID；飞控按钮继续通过既有 Fact 参数读写，保留两类动作的互斥规则和缺少参数时的禁用提示。
- 同一设备／载具的控制器在切页时保持存活，切换设备／载具时重建。修复 QML 初始绑定前查询扩展轴的空引用，并以 `QPointer` 防止拔出设备后的失效引用和重复清理轮询。

本轮代码范围为 `src/Toolbar` 中上述两个控件、七个手柄／遥控配置源文件、三个相关测试文件与中文翻译。此前已提交的退出二次确认、重复连接告警设置清理继续有效。原 1.5.0 目录未修改。工作区内同时出现的 Windows 构建／打包脚本与文档变更由其他任务产生，未纳入本轮 UI 改动。

## 验证结果

- Debug 增量构建成功，过程日志保留在 `.tmp/codex/ui-optimization-20261007/build-final.log`。
- 三组定向 CTest 全部通过：`JoystickTest`、`JoystickControlTest`、`AppSettingsTest`，共 24.21 秒。日志为 `build-v5.1.5-debug/ui-optimization-final-tests.log`，JUnit 与结果 JSON 使用同名前缀。
- Windows 原生 `DeepSharkUILayoutTest` 通过，5 项测试（含初始化、清理），0 失败／0 跳过，38.619 秒。覆盖顶部按钮相对位置、MockLink 解锁／上锁、固定按钮宽度、默认校准页、校准期间禁用切页、四页切换时控制器不重建、按键高亮、设备拔出后的控制器清理，以及既有中文设置页面和自定义窗口回归。
- 原生测试使用 ArduSub MockLink 与 SDL 虚拟手柄；日志和 XML 为 `.tmp/codex/ui-optimization-20261007/native-final*`。界面未出现新的 `TypeError`、空绑定赋值或控制器内部清理错误。
- 七个改动 QML 文件的 SDK `qmllint` 均以 0 退出；保留已有未限定属性访问等建议。八项适用的非格式 pre-commit 检查通过，`git diff --check` 通过。
- 匹配项目要求的 clang-format 22.1.5 检查显示整文件门禁仍有既有诊断：`JoystickConfigController.cc` 89 处、其头文件 3 处、`JoystickControlTest.cc` 2 处。逐项与提交基线的源行和诊断位置比对，没有新增格式诊断；新增原生测试文件格式通过。证据为 `.tmp/codex/ui-optimization-20261007/format-baseline-comparison.json` 及对应日志。未扩展为全文件格式重写。

按现有定向验证约定，本轮没有再次运行全仓库 Unit 测试或制作安装包。功能测试后仅规范 25 条新增中文条目的 XML 缩进，译文未变，XML 解析与再次增量构建通过；未重复运行功能测试。最终 Debug 程序为 `build-v5.1.5-debug/Debug/QGC_KevinJiang_v5_1_5_Debug.exe`，最终 SHA256 为 `1B7F0005E6C92DDC7E985BA9B5F6BC5575EC175DE6FEF857F68B47DF2D03CB64`。可通过现有 `tools/debug/start-windows-debug.ps1` 或 Debug 快捷方式启动。

## 预览与实物验收边界

原生窗口截图保存在 `.tmp/codex/ui-optimization-20261007/`：

- `toolbar-arm-heartbeat.png`
- `joystickGeneralTab.png`
- `joystickButtonsTab.png`
- `joystickCalibrationTab.png`
- `joystickAdvancedTab.png`

截图使用测试载具和虚拟手柄，模式可能显示 `Unknown`，飞控动作枚举来自 MockLink 的测试元数据。真实载具沿用固件提供的模式和参数枚举。

设备拔出时，既有 JoystickManager 重扫描路径可能尝试重启配置轮询，产生断开、重新打开失败等告警。原生测试仅在虚拟设备移除阶段明确允许这些已知告警，保留原始日志；没有屏蔽 QML 绑定错误或控制器内部错误，也没有扩大本轮范围修改 SDL 后端。

真实手柄的完整校准、现有按钮映射、运行时动作触发和实际操纵手感仍需用户使用实物确认；上述自动化结果不能替代硬件验收。
