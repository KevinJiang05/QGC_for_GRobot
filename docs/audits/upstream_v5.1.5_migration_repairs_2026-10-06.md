# QGC v5.1.5 升级遗漏修复记录

基线：升级前 `db9c9159631a271457a7d6c395deb50e2cf3c304`；官方
`v5.1.5` 为 `3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`。
修复在当前 `main` 上进行。用户已批准依据
[原始复核结论](20261006_5.1.5_bug.md)分阶段修复，原始结论文档不覆盖。
本轮已完成下表中的源代码修复及受控验证，交付当前 Debug 供实机验收。
受控验证不代表实机或正式安装验收完成。

用户随后明确收窄验收范围：无人机、固定翼等通用功能暂不处理，
只验证此前 DeepShark/ArduSub 二次开发接入；不以全部官方测试通过为目标。
旧官方驱动安装、32 位旧安装检测和 `-desktop` 入口不属于本地定制差异，
本阶段沿用 v5.1.5 实现并列为后续评估项，不恢复这些上游旧功能。

## 修复清单

| 范围 | 当前状态 | 后续验收 |
| --- | --- | --- |
| 旧中文、上下文改名、源码转 JSON、定制设置标题 | 已修复并验证编译后的译文；已打开原生中文视频设置页 | 其他页面逐页及实机确认 |
| 停止脚本、新 Debug 的独立 YOLO 配置选择 | 已修复；受控配置测试与停止脚本只列出测试通过 | 实际 AI/进程启停 |
| 当前手柄选择、逐车辆启用状态 | 已补当前应用配置中的旧键迁移；受控测试通过 | 正式身份下的真实配置升级与手柄实测 |
| 旧云台轴与新版辅助轴语义 | 已核对基线实际发送链路，见下方更正 | 真实按钮/云台操作 |
| 拍摄按钮、计数、设置入口横向布局 | 已恢复；照片/视频、设置入口、连续拍照停止的原生测试通过 | 设备端拍摄 |
| 旧 MNT_、新版 MNT1_/MNT2_ 与可选参数页面 | 已修复；6 组参数夹具与页面实例化通过 | 旧固件实机 |
| AI 安装内容、Kevin 桌面入口、安装器版本、卸载返回码 | 源代码已恢复；预检与源代码契约检查通过 | 稳定后安装包与覆盖升级验证 |
| 标准发布脚本与 main 构建触发 | 脚本已适配；新增 main 的 Windows Debug 工作流，YAML/actionlint 通过 | 远端未运行；安装包未生成 |
| 二次开发相关回归与定制差异验收 | 11 个所选类、AI 7 项和原生界面通过；35 个 custom 文件与 13 个资源引用保留 | 实机路径及剩余 UI 显示限制见下文 |

## 中文修复与证据

之前的脚本只搬运相对旧官方的本地译文增量，并以完整上下文精确匹配；
未覆盖上下文改名、跨源码/JSON 目录迁移和旧版已有的官方中文。
本轮从项目基线两份中文目录读取全部已完成中文，按原文、消歧及复数条件
建立候选，再核对上下文与占位符；多个旧译文有歧义时逐项选择。

- 恢复 825 条现有消息：源码 637 条，JSON 188 条。
- 补入 4 条新上下文中的定制标题：视频页的 DeepShark 解码器、AI 检测、
  连接告警，以及链路页的连接告警。
- 原生截图另发现改名标签 `Force video decoder priority`，依据对应旧参数
  补回“视频解码优先级”；旧设置中已翻译的同类改名标签已对照。
- 当前源码目录 4,090 条消息、1,893 条完成（另补云台“手柄速度”和“自稳”）；
  JSON 1,825 条消息、439 条完成。
  条目数不等于界面覆盖率；旧版本来未翻译或新版新增的英文不称为迁移恢复。

受控迁移过程及条目来源保存在项目忽略目录：
`.tmp/codex/chinese-restored-entries.json`、
`.tmp/codex/settings-label-review.json`。
`AppSettingsTest` 的 12 条译文用例加载实际编译并嵌入程序的 QM，
覆盖新 JSON 上下文、原上下文回退和定制标题。

2026-10-06 02:32 原生 `DeepSharkUILayoutTest` 结果：4 项通过、0 失败、0 跳过
（含初始化/清理；两项行为为主窗口关闭重建和中文设置页）。证据：
`.tmp/codex/migration-ui-render-final/native-ui-results-DeepSharkUILayoutTest.xml`、
`native-ui-tests.log`、`native-ui-result.json`、
`deepshark-chinese-video-settings.png`。
它证明视频设置页面真实实例化及标题翻译；不证明全部设置页面已验收。
此前两次复测的标题断言已通过，但等待下一次 `frameSwapped` 超时；
改为直接检查并保存非空窗口抓图，同时保留主窗口关闭重建中的帧信号检查。
失败记录保留在 `migration-ui-final` 与 `migration-ui-final-fixed`，没有算作通过。
截图中的 `Application Settings` 在旧版目录中也未翻译；新增说明和搜索文案
不属于已有中文的迁移遗漏。本轮没有承诺补齐所有新版中文。

## 启动、AI 与手柄设置

`StopDeepSharkQGC.cmd` 同时识别原程序和新 Debug 程序，沿用子进程停止路径。
测试使用替代进程列表且只执行 `--list`，确认列出两种程序及其 AI 子进程，
排除无关进程；没有停止实际设备或进程。

独立 YOLO 入口优先读取新 Debug 配置；显式指定配置时仅读取该文件。
新配置不存在 RTSP 源时不再回退读取其他应用的视频地址。
2026-10-06 最新 AI 回归 7/7 通过，使用临时 INI 与已有受控夹具，
没有读取用户实际配置。

手柄兼容只处理当前应用设置：
`JoystickManager/ActiveJoystick` 转到 `activeJoystickName`；
`Vehicle<ID>/JoystickEnabled` 转到 `joystickEnabledVehiclesIds`。
已有新键（包括主动禁用的空列表）优先，旧键保留。
此前校准、按钮迁移继续沿用原实现与校准防护。
3 个相关测试类通过的证据为
`build-v5.1.5-debug/migration-settings-final-{tests.log,junit.xml,test-result.json}`。
Debug 身份仍独立于正式身份，不自动读取正式配置。

### 对旧云台轴结论的更正

基线 `src/Joystick/Joystick.cc` 虽然在约 611–620 行计算云台俯仰和偏航值，
但约 676 行实际调用
`sendJoystickDataThreadSafe(roll, pitch, yaw, throttle, shortButtons)`，
没有传入这两个云台轴值。不能据此声称升级删除了基线中有效的云台轴发送功能。
新版辅助 MANUAL_CONTROL/RC_OVERRIDE 的语义不同，直接转换旧键会新增控制行为。
因此保留旧键，继续不自动转换；旧云台按钮路径仍需设备确认。

## 拍摄布局与旧云台参数

拍摄按钮、时间/计数、齿轮在新版控件内恢复横向排列，保留新版独立拍照、
录像及连续拍照行为。设置中的拍照间隔改为 `onMoved` 写入，避免仅打开
对话框时，数值绑定触发回写及绑定循环。
原生 `FlyViewCameraCaptureUITest` 5 项通过、0 失败、0 跳过，包含初始化/清理，
两种模式的左右顺序、对齐和齿轮点击，以及连续拍照再次点击停止。
02:18 与 02:25 两次通过的记录分别位于
`.tmp/codex/migration-capture-coordinates/` 与 `.tmp/codex/migration-capture-repeat/`，
各自保留 XML、日志、退出码及 `photo-layout.png`、`video-layout.png`。

离屏 `FlyViewCameraCaptureUITest` 曾因 Qt 6.11 数值断言中止。
`.tmp/codex/capture-assert-stack.log` 表明中止发生在 `startUI` 的窗口曝光阶段，
尚未运行模拟相机/布局测试；此失败保留，不称为通过，也不扩大修复到通用离屏平台。
该测试中的模拟相机仅作为拍摄定制控件的夹具，不是无人机控制功能验收。

`APMGimbalParams` 按实际参数区分旧 `MNT_` 与新 `MNT1_/MNT2_`，两种同时存在时
使用新版。缺失可选项返回空 Fact 并隐藏相应编辑项；旧角度仍使用原 Fact 和单位，
旧 RC 输入、自稳、手柄速度分别接入，不与新版 RC_OPTION/RC_RATE 混用。
旧伺服名称通过存在性检查选择 SERVO 或 RC 参数。

`APMGimbalCompatibilityTest` 的 6 组夹具覆盖旧参数、旧可选参数缺失、两个新版
实例、新版优先、需要重启和无云台支持。实际实例化参数对象及配置控件，
检查所有输入 Fact 的原值未被页面改写。最新结果：
`build-v5.1.5-debug/migration-gimbal-final-test-result.json` 为成功，
对应 `migration-gimbal-final-tests.log`、`migration-gimbal-final-junit.xml` 保留。

## 发布源代码契约

恢复 `bin/ai_detection` 安装规则、Kevin 安装器 PE 版本资源、桌面快捷方式创建
和删除、旧卸载器退出码 `$0` 捕获；这些均是基线本地定制内容。
标准发布脚本改用新 Help 页路径、Qt/GStreamer 共享环境、新独立 Release 构建目录，
从生成的 `QGC_WINDOWS_OUT` 读取带架构后缀的安装器路径，并接受一致的三段/四段
产品版本格式。安装后验证清单包含 AI 三个核心脚本。
2026-10-06 02:15 仅执行 `-Version 1.5.0 -PreflightOnly`：
`dist/QGC_KevinJiang_v1.5.0/release-audit/20261006-021509/release-state.json`
为 `preflight-succeeded`、`artifact: null`。
另以 PowerShell AST 只加载版本规范化函数，检查三段/四段版本、修订位差异
及非法格式；对照 NSIS 源码契约和 Debug 生成的 AI 安装规则。
结果为 `.tmp/codex/migration-release-contract.json`。
没有运行 Release 配置、编译、打包或安装；预检只产生审计报告和空暂存目录。

## 定制功能接入与验证对应

以下按升级前项目基线核对，不要求无人机/固定翼等全部官方功能通过。

| 原有内容 | 新版保留/修复方式 | 受控证据与边界 |
| --- | --- | --- |
| 四路视频、地图遮挡、视频生命周期 | 新 FlyView、DeepShark 控制器与 custom 资源 | `DeepSharkVideoControllerTest`、原生窗口重建；没有真实 RTSP 长稳 |
| AI 设置、接收、覆盖层 | 现有管理器/接收器；独立脚本增加新 Debug 配置 | `AIDetectionReceiverTest`、Python 7 项；没有真实推理 |
| AUV 任务 | 现有控制器与新模块导入 | `DeepSharkAuvControllerTest`；实际飞控动作未验证 |
| 推进器映射、直接控制和恢复 | 保留原控制器/入口 | `ThrusterDirectControlControllerTest`；实际写入和输出未验证 |
| 连接告警、主动断开区分、默认 TCP 与 NoProxy | 保留原监控/链路接入 | `DeepSharkConnectionMonitorTest`、`VehicleLinkManagerTest`；设备链路待测 |
| 手柄校准、按钮、选择和逐车辆启用 | 保留原迁移，补 manager 旧键 | `JoystickTest`、`JoystickManagerTest`；真实手柄/正式身份未测 |
| 拍照/录像控制布局 | 在新版行为上恢复横向控件 | 相机管理器及原生拍摄测试；设备拍摄未测 |
| ArduSub 安全、灯控、固件入口 | 保留升级适配中的可选参数防护 | 代码存在；实际旧参数集、刷写未测，不称完整实机兼容 |
| 旧云台参数页 | MNT_ 回退与现代参数优先，可选项隐藏 | 6 组参数夹具；旧伺服选择和设备写入待测 |
| 状态、3D 姿态、品牌与资源 | custom 文件与资源引用保留 | 窗口对象实例化；真实遥测与 GPU 效果未测 |

本轮复查结果 `.tmp/codex/migration-baseline/result.json`：
35 个基线 custom 文件均存在，17 个内容相同、18 个为升级适配；
`custom.qrc` 的 13 个引用文件全部存在。本轮未新增任何主动功能删除。
旧 Qt 包装及上游 API 替换仍按原复核记录区分，不改称已获逐项批准。

## 构建与检查范围

Windows Debug 增量编译、链接通过。11 个所选测试类覆盖上述控制器、
云台、手柄、设置、相机管理和链路管理，没有重跑全部官方测试。
最终证据位于 `build-v5.1.5-debug/`：
`migration-custom-acceptance-tests.log`、`migration-custom-acceptance-junit.xml`、
`migration-custom-acceptance-test-result.json`。
最终 11/11、0 失败、0 超时，用时 62.20 秒；
综合证据索引为 `migration-acceptance-summary.json`。
AI 7/7 的日志及退出码为 `migration-ai-tests.log`、`migration-ai-test-result.json`。

`.github/workflows/kevin-windows-debug.yml` 适配 `main` 推送/PR/手动触发，
只构建 Debug 并运行这 11 个类与 AI 测试；官方多平台工作流保留。
新工作流尚未推送和远端执行，因此不能称 GitHub 构建已通过。

限定检查中 XML/JSON/YAML、Python、Markdown、actionlint 及相关代码规则通过。
`cmake-format` 和 `cmake-lint` 仍报告 Install.cmake 的原有问题；
用同一工具和配置对比 HEAD 与当前文件，7 条 lint 问题完全相同，
格式检查两者均失败，证据位于 `.tmp/codex/migration-baseline/`。
新工作流的 zizmor 另有 4 条 LOW：建议把沿用上游的 `./` 本地 action 引用
改成 GitHub 新的 `$/` 语法。当前仓库固定版本的 actionlint 不识别新语法，
本轮保留现有结构，没有升级上游工具或压制告警。
限定 lint 总体仍非全绿，不能据此称全部检查通过。

当前可实测程序为
`build-v5.1.5-debug/Debug/QGC_KevinJiang_v5_1_5_Debug.exe`，
产品版本 `1.5.0.0`、公司 `KevinJiang`，SHA-256 为：
`043368aec42159354b68efcbe7965403569709f625d1c2248eb9a358f8b6bfd6`。
仍使用 `StartDeepSharkQGC.cmd` 启动；此次没有改为 2.0.0。

相机测试截图在本机高 DPI、紧凑窗口中还显示：原有视频工具条过宽，
部分按钮被裁剪，“状态”按钮靠近相机齿轮；齿轮中心点击已通过。
工具条排列和状态按钮锚点与基线一致，尚未与旧运行产物同尺寸对照，
不能确认这是升级新问题；本轮记录此显示限制，没有扩大为界面重构。

## 验收边界

当前只构建 Debug，不制作或安装 Release 包；产品仍为 1.5.0 Debug，
本轮没有擅自改成 2.0.0。没有连接实际飞控、摄像头或手柄。
本机受控配置和 MockLink 结果不能替代真实参数写入、ACK、推进器、RTSP/AI
长稳及安装覆盖升级验收。编译成功不等于迁移完整。

本轮尚未提交或推送。软件修复和限定受控验证完成；正式配置、实机、
安装包及上述未验证项仍按各自边界保留，不宣布迁移全部验收完成。
