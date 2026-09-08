# 2026-08-17 电流/电压限幅复核

## 当前参数快照

| 参数 | 当前值 | 解释 |
| --- | ---: | --- |
| `MOT_BAT_CURR_MAX` | 0 | 电机最大电流限幅关闭 |
| `MOT_BAT_VOLT_MIN` | 0 | 电池电压补偿下限关闭 |
| `MOT_BAT_VOLT_MAX` | 0 | 电池电压补偿上限关闭 |
| `BATT_LOW_VOLT` | 0 | 低电压阈值关闭 |
| `BATT_CRT_VOLT` | 0 | 临界电压阈值关闭 |
| `BATT_FS_LOW_ACT` | 0 | 低电压 failsafe 不执行动作 |
| `BATT_FS_CRT_ACT` | 0 | 临界电压 failsafe 不执行动作 |
| `MOT_OPTIONS` | 0 | 未启用额外电机选项 |
| `MOT_SPIN_MAX` | 0.95 | 电机归一化输出在约 95% 处饱和 |
| `MOT_PWM_MIN/MAX` | 1100/1900 | 正常 PWM 端点 |
| `MOT_THST_EXPO` | 0.65 | 非线性推力曲线，影响中间段，不是电流限幅 |

参数元数据对 `MOT_BAT_CURR_MAX` 明确写明 `0 = Disabled`，对电压补偿上下限也明确写明 `0 = Disabled`。因此当前参数没有发现“电流超过阈值后压低最大油门”的启用配置。

## 当前判断

- 电流/电压保护限幅不是当前参数层面的首要嫌疑。
- `MOT_SPIN_MAX=0.95` 值得保留为轻微最大输出缩减因素，但它不能解释 50/75/100 增益下完全没有物理响应差异。
- 若要证明存在代码级隐藏限制，需要在固定 `SERVO_OUTPUT_RAW`/物理 PWM 下读取 ESC 电流、转速或推力；仅凭 `BATTERY_STATUS` 不能证明电机限流。

## 源码路径复核

在与固件版本提交 `f3a6fa9d` 对应的 ArduSub 4.8 源码中，`AP_Motors6DOF::get_current_limit_max_throttle()` 直接返回 `1.0f`。`output_armed_stabilizing()` 的电流限制分支在 `_batt_current_max <= 0.0f` 或电池电流不可用时直接返回；只有设置了正的 `MOT_BAT_CURR_MAX` 且能取得电流时，才会按预测电流降低 `_output_limited`。

这与当前参数 `MOT_BAT_CURR_MAX=0` 一致，没有发现标准 ArduSub 6DOF 路径会在当前配置下暗中施加电流限幅。仍需保留“自定义固件未合入该源码”的可能性，但当前动态 PWM 对比和参数/源码路径均不支持该假设。

## HEX 静态检查进展

HEX 中存在完整的电机/电池参数字符串和日志字段，包括 `MOT_PWM_MIN`、`MOT_PWM_MAX`、`MOT_THST_HOVER`、`SPIN_MAX`、`BAT_VOLT_MAX`、`BAT_VOLT_MIN`、`BAT_CURR_MAX`、`TimeUS,LiftMax,BatVolt,ThLimit,ThrAvMx,ThrOut,FailFlags`。这表明相关标准功能确实被编译进镜像，但字符串本身不能证明运行时一定触发限制。

当前 HEX 没有配套 ELF 符号表；仅凭 HEX 反汇编可以继续定位 Thumb-2 浮点比较和电池电流调用，但无法像源码/带符号 ELF 一样可靠命名每个函数。现阶段最强证据仍是：参数为关闭值、标准源代码在该条件下跳过限流、动态软件/物理 PWM 均按增益变化。
