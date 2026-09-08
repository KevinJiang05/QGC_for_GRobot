# 2026-08-17 兼容性只读监听

## 条件

- 连接：`tcp:192.168.1.200:4019`
- 监听时长：约 15 秒
- 监听程序只接收 MAVLink，不发送控制、解锁或执行器命令
- 飞控状态：`MAV_TYPE=SUBMARINE`，`MAV_AUTOPILOT=ARDUPILOT`，MAVLink 2，未解锁

## 结果

- 收到 `HEARTBEAT`、`SYS_STATUS`、`BATTERY_STATUS`、`NAMED_VALUE_FLOAT` 等正常遥测。
- 收到 30 个 `SERVO_OUTPUT_RAW` 消息，周期约 500 ms。
- `SERVO3_RAW` 至 `SERVO10_RAW`（参数功能 33–40，Motor1–Motor8）均为 `1500 us`，符合未解锁中位状态。
- `SERVO12_RAW=1300 us`，但参数 `SERVO12_FUNCTION=187` 为 `Actuator4`，不是推进器电机通道；该值不能作为推进器动力证据。
- 监听期间没有收到 `MANUAL_CONTROL` 消息。

## 判读

这次监听证明 MAVLink 遥测及伺服输出回报链路正常，但不能证明手柄满量程输入链路，因为当前 QGC/手柄没有发出 `MANUAL_CONTROL`。后续若要验证 QGC 输入，必须在 QGC 确认手柄已启用并保持车辆未解锁的前提下，再做同样的被动抓包；抓包期间仍不发送任何控制命令。

## 第二次监听

- 监听时长：约 20 秒
- `MANUAL_CONTROL`：0 条
- `PilotGain`：持续为 `0.5`
- `SERVO3..SERVO10`：持续为 `1500 us`
- 心跳：持续 `base_mode=81`，未解锁

第二次监听仍未观察到 QGC 手柄包。优先检查 QGC 是否实际连接到 `192.168.1.200:4019`、Joystick 是否启用，以及手柄是否被系统识别。
