# v5.1.5 手柄与中文界面修复验收

日期：2026-10-06。按用户确认的两阶段范围，在当前 `main` 修改。本报告仅描述手柄、中文与相应测试；共享工作区另有其他对话的姿态面板改动，不归入本轮交付。没有提交、推送或制作安装包。运行的是独立身份的 Windows Debug 程序。

## 第一阶段：手柄

- 旧配置迁移不再用默认校准值替代缺失字段。五个校准字段、轴映射、数值转换、SDL 范围与死区均校验；异常配置不会标为已校准。保留旧键，已有 V2 配置优先。
- V2 实际加载也检查这些条件，并拒绝重复功能映射。合法四轴、中心在端点的单向辅助轴仍可加载。无效校准沿用现有禁用/重新校准流程。
- ArduSub 普通/Shift 飞控动作以“非零”判断互斥，包括 int8 中以负数表示的扩展枚举。选本地 QGC 动作时清零两个飞控 Fact；选飞控动作时清除本地动作。继续复用参数 Facts 和既有参数就绪/缺失保护。
- 内置 QGC 按钮显示中文标题，保存与查找仍使用原始 action ID。未知或旧自定义动作保留；不会将中文标题写入动作配置。连接载具使模式动作列表插入新项时，按稳定 ID 刷新当前选择与已分配显示，附有列表变更测试。
- 新增 `JoystickControlTest`：实际 SDL 虚拟手柄、JoystickManager/Vehicle/ArduSub MockLink 链路核对 MAVLink `MANUAL_CONTROL` 的四轴、中立值、反向、死区、油门模式、`buttons` 和 `buttons2`。另用真实按钮 QML 组件验证中文选择、动作互斥、延迟/缺失参数。
- 补齐迁移/加载的异常与正向测试，包括单向辅助轴、重复映射及中文当前选择。

轴校准、反向、死区、本地 QGC 动作属于地面站配置；飞控的 `BTNx_FUNCTION` / `BTNx_SFUNCTION` 属于飞控参数。手柄轴与按键状态通过控制报文发送给飞控。Debug 使用独立设置身份，不会自动读取已安装正式版的手柄配置；本轮没有读写用户真实手柄设置或连接实机。

既有 V2 配置若已被更早版本填入看似合法的默认值，本轮无法判断它们是否来自真实校准。换用 Debug 或对校准来源无把握时，应重新校准。

## 第二阶段：中文

复用现有 Qt TS/QM 与 JSON 元数据翻译机制，没有添加翻译框架、依赖或外置语言包。

| 项目 | 本轮结果 |
| --- | --- |
| 原有 unfinished 条目补齐 | 1896：源代码 870、JSON 1026 |
| 当前代码缺少目录项的文案新增 | 103：源代码 46、JSON 57 |
| 旧手柄标题修正 | 4：连续放大/缩小、逐步放大、VTOL 多旋翼标题换行 |
| 全部中文目录仍 unfinished | 1687：源代码 1327、JSON 360 |

范围覆盖日常设置、菜单、通信、视频/AI 设置、手柄、ArduSub 设置及常用状态、错误、确认提示。沿 ArduSub 的实际入口补齐新版 JSON 生成的电源、失效保护与日志页面，以及 ESC 和运行安全组件文案；当前定义的范围中没有 unfinished 条目。其余 unfinished 多属本轮之外的功能，不能宣称全软件已百分之百汉化。技术缩写、协议名、参数标识与内部诊断字段按含义保留原文；英文搜索关键词同时保留，并加入中文。

`Depth Hold`、`Surface`、`Motor Detection`、`Surftrak` 在当前实现中兼作旧手柄动作标识，保留本轮开始时的英文值，避免语言切换使已有按钮失效。其他既有模式译文未改变。`System` 语言选项补入翻译调用，显示“跟随系统”。另核对自定义源目录，补齐 AI 浏览按钮、重启/停止提示、自定义视频状态和导出提示；本来已写成中文的自定义源文案无需新增重复条目。

逐条清单：`.tmp/codex/joystick-and-chinese/translation-final-manifest.json`。
结构检查：`.tmp/codex/joystick-and-chinese/translation-validation.json`。
检查确认原有消息键未删除或改变；新增项匹配当前源码/JSON；Qt 占位符、枚举/关键词数量、富文本标签、换行和地图模板占位符均通过。

## 验证与边界

- Windows Debug 增量构建和链接通过，中文 QM 已编入程序。
- 五个手柄测试类最终通过：`JoystickTest`、`JoystickControlTest`、`JoystickManagerTest`、`MockJoystickTest`、`SDLTest`。日志 `build-v5.1.5-debug/joystick-chinese-final-candidate-tests.log`。其中 JoystickManager 的三项多设备用例因当前 SDL 后端未暴露足够并发虚拟设备而跳过；不算多手柄实测通过。
- 原生 `DeepSharkUILayoutTest` 通过，含窗口重开以及常规、控制视图、视频页面。特别断言当前带 ArduPilot 限定的盘旋半径说明，防止旧目录键造成英文回退。最终证据 `.tmp/codex/joystick-chinese-delivery-ui/`，含 XML、结果 JSON 和截图；截图已人工查看。
- 限定改动的适用 pre-commit 检查（不适用项跳过）、C++ 改动范围格式和新增测试文件格式通过。QML 使用现有 SDK 与生成模块的导入路径检查通过（仍有既有未限定属性访问建议）。日志 `build-v5.1.5-debug/joystick-chinese-lint-results.json`、`joystick-chinese-cpp-format-result.txt`、`joystick-chinese-qmllint-sdk.log`。没有执行会改动全仓库其他文件的格式修复。
- 最终构建的 Unit 验收 `joystick-chinese-delivery-unit` 为 **204/204 通过，426.18 秒**。与前轮相比，共享工作区另注册了一项姿态面板测试，因此入口总数增加；该功能不归入本轮改动。
- 中间验证 `joystick-chinese-unit-verified` 为 203/203 通过（377.18 秒）。后续候选 `joystick-chinese-final-unit` 为 202/203 通过：`GeoTagControllerTest` 在 `_fullGeotaggingTest` 出现一次 Qt 共享数组断言（`qarraydataops.h:86`，退出码 `0xc0000409`）。该模块未在本轮改动，原因尚未定位，不称为已修复。异常 XML 已保存在 `.tmp/codex/joystick-and-chinese/geotag-intermittent-assert.xml`。最终构建单项连续复跑三次及最终整套中的该项均通过，日志前缀为 `joystick-chinese-geotag-recheck-`。

最终 Debug 程序：`build-v5.1.5-debug/Debug/QGC_KevinJiang_v5_1_5_Debug.exe`。验收前后 SHA256 均为 `F5D794C59118C8470751966AECE27144A07E6D9C501D8B251D95D629D92B3ECF`，可用现有 `tools/debug/start-windows-debug.ps1` 启动。

用户在最终验收时明确要求避免随意全量测试。本轮重复整套验证过多；后续默认按影响范围运行定向测试，公共链路变化或发布验收确有需要时再判断全量范围，不因常规翻译或小幅界面调整重复整套。

第一轮全 Unit 找到三个先前也失败的类：`QGCMAVLinkTest`、`SysStatusSensorInfoTest`、`SigningControllerTest`。失败是中文显示值与硬编码英文/错误翻译上下文的测试期望不同（VTOL、Error、Disabled、Off）。本轮仅将这些显示值期望改为对应模块的翻译上下文，保持枚举映射、状态和顺序断言，内部标识断言仍保留英文。另修正本轮中文化后暴露的 `FactMetaDataTest`、`StatusTextHandlerTest`、`JsonHelperTest`、`JsonParsingTest`：重启/未知键消息使用正确翻译上下文，缺失日志分块的省略号使用对应译文与消歧说明，JSON 列表错误核对完整译文和实际数量。第一轮 203 个入口中 196 个通过、上述 7 个失败，用时 386.72 秒；这些断言修正后重新执行整套验收。没有因此修改车辆、传感器、签名或解析器生产逻辑。

## 独立复核

手柄 Claude 只读复核原文保存在 `.tmp/codex/joystick-and-chinese/claude-joystick-review-verbatim.md`，完整 JSON 同目录 `claude-joystick-review-connected.json`。结论原文：

> 结论：本轮代码里没有发现必须修的逻辑缺陷。校准验证、稳定 action ID、负数枚举互斥这三块都对。但测试有两处缺口，抓不到"误拒合法校准"和"当前选择错位"这两类回归；另外中文标题上屏后，暴露出一处已有的翻译问题。

处理：补了合法 V2、单向辅助轴、重复功能、旧迁移端点中心和中文当前选择断言；修正四个旧标题；最终重新构建并通过手柄测试。复核只覆盖当时的代码，没有代替实机验收；之后的动作列表刷新由 Codex 与回归测试确认，没有追加外部复核。

另开中文语义复核会话时，Claude 返回原文：

> You've hit your weekly limit · resets Oct 9, 9am (Asia/Taipei)

原始回复保存在 `.tmp/codex/joystick-and-chinese/claude-chinese-review.json` 和 `claude-chinese-review-verbatim.md`。这轮外部复核未完成，不计为通过。按用户最新 AGENTS 约定，暂停后续 Claude 调用。Codex 已检查本轮文案、关键控制提示、当前目录匹配和实际界面；整体译文仍可由团队进一步校对。

## 真实手柄与机器人验收清单

1. 在当前 Debug 身份中识别实际手柄并完成校准；核对四个基本轴、单向扳机、中立、反向、死区、模式与升沉行为。重启后确认配置与按钮选择保留。
2. 接入 ArduSub，等待参数就绪；验证普通与 Shift 动作，尤其扩展枚举：选择飞控动作应清除 QGC 动作，选择 QGC 动作应让两个飞控参数归零。核对实际参数确认而不只看下拉框。
3. 检查按键按下/释放、连续/单步动作，以及超过第 16 个按键时的实际固件响应；虚拟报文测试不证明实机支持所有按键或扩展轴。
4. 拔插手柄、断开/重连飞控并重启地面站，确认启用状态、动作归属和控制恢复。当前三项多设备自动测试有跳过，多手柄切换需实测。
5. 操作者查看中文菜单、手柄标题、视频/AI 和通信设置，确认关键术语符合团队习惯。

完成以上实测前，本报告只表示代码、模拟链路与界面验证结果，不表示机器人已经完成操作验收。若要分发安装包，仍须使用标准 Windows 发布脚本及其发布报告。
