# 2026-08-17 PilotGain 直连定量测试

## 条件

- QGC 已关闭，避免多个客户端同时发送手动控制。
- MAVLink GCS system ID 使用参数要求的 `MAV_GCS_SYSID=255`。
- 模式：Manual (`custom_mode=19`)。
- 固定输入：`MANUAL_CONTROL x=0, y=0, z=600, r=0`。
- 每个增益保持约 2 秒，并以约 10 Hz 采集 `SERVO_OUTPUT_RAW`。
- 测试结束发送中位和解除锁定命令，并由心跳确认 `armed=false`。

## 结果

升沉输入驱动 Motor2、Motor3、Motor5、Motor7，对应本机 `SERVO4/5/7/9`：

| PilotGain | SERVO4/5/7/9 | 其余 Motor 通道 |
| ---: | ---: | ---: |
| 0.50 | 1460 us | 1500 us |
| 0.75 | 1440 us | 1500 us |
| 1.00 | 1420 us | 1500 us |

每个阶段取得 20 个样本；除切换开始的首个中位样本外，其余样本稳定一致。变化幅度严格符合上游固件公式：相对中位分别为 40、60、80 us。

## 判读

1. PilotGain 不是“显示变化但未进入输出”；它确实进入了 ArduSub 混控，并改变 `SERVO_OUTPUT_RAW`。
2. QGC/ArduSub 的 `MANUAL_CONTROL` 字段契约和 ArduSub 增益计算均正常。
3. 现场观察到 PilotGain 50/75/100 时推进器声音和风量无变化，因此故障边界位于 `SERVO_OUTPUT_RAW` 之后：板级定时器/物理 PWM 波形、输出口映射、ESC 输入范围或推进器动力链路。
4. `SERVO_OUTPUT_RAW` 是飞控软件命令值，不是对物理引脚脉宽的回读。下一项决定性证据应为对应物理输出口的实际脉宽测量。

## 示波器补充验证

现场随后使用 QGC 自研 PWM 直发功能和示波器验证：直发 PWM 值、飞控输出报告与物理引脚脉宽一致；正常控制中的 PilotGain 限幅也在物理波形上生效。

因此板级 PWM 输出层验证通过，QGC/固件兼容性不再是当前主要嫌疑。后续转向 ESC 输入端点、输入协议、响应曲线以及推进器负载/供电侧。

## 安全结束状态

- 飞控心跳：`base_mode=81`
- `armed=false`
- PilotGain 最终值：1.0（与测试开始时相同）
