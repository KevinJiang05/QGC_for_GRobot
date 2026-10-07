# Kevin v2.0.0 / QGroundControl v5.1.5 Windows Debug 候选版

本候选基于官方 `v5.1.5`（`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`），保留 DeepShark 定制功能。
升级前检查点为 `db9c9159631a271457a7d6c395deb50e2cf3c304`；长期开发分支统一为 `main`，版本及回退节点使用 tag 记录，见 [仓库维护说明](../development/repository-maintenance.md)。

已执行的验证、此前失败/超时的修复及全量 lint 的保留问题，见 [Debug 验证记录](../audits/upstream_v5.1.5_debug_validation_2026-10-05.md)。

## 启动

直接双击项目根目录的 `StartDeepSharkQGC.cmd` 即可启动此候选版。该入口固定使用本候选构建目录。
需要诊断日志时，从项目根目录执行 `StartDeepSharkQGC.cmd --diagnostics`。
启动器的 stdout/stderr 日志分别保存在 `%TEMP%\QGC_KevinJiang_v5_1_5_Debug.stdout.log` 和 `%TEMP%\QGC_KevinJiang_v5_1_5_Debug.stderr.log`。

也可以在 PowerShell 中执行：

```powershell
Set-Location D:\Develop\QGC_for_GRobot
.\tools\debug\start-windows-debug.ps1
```

实际程序：

`D:\Develop\QGC_for_GRobot\build-v5.1.5-debug\Debug\QGC_KevinJiang_v5_1_5_Debug.exe`

请通过启动脚本运行。脚本为目标进程设置 Qt/GStreamer 路径，程序结束后恢复当前 PowerShell 的环境；无需安装或更改系统环境变量。
此目录直接使用共享开发 SDK，不是可复制到其他电脑的安装包。

## 配置隔离

- 显示版本：`2.0.0 Debug (QGroundControl v5.1.5)`。
- 普通启动的应用名：`QGC_KevinJiang_v5_1_5_Debug Daily`，组织名 `KevinJiang`。
- QSettings、应用缓存和默认保存目录使用上述独立应用名；测试模式另外使用带测试名或 PID 的配置。
- Debug 不自动读取正式版配置。首次实测需要在候选界面重新填入视频、AI 和手柄设置；正式版设置及校准保持原样。
- 旧手柄兼容代码只在当前应用的设置存储中，将 `Joysticks/<名称>` 转成 `JoystickSettingsV2/<名称>`，并迁移旧手柄选择及逐车辆启用状态；已有新键优先（包含主动禁用的空列表），旧键保留。旧云台轴不自动转换成含义不同的辅助控制轴；基线计算出的云台轴值没有传入实际手柄发送调用，不能把此项称为已经验证的旧控制功能。

## 依赖与构建

| 用途 | 位置 |
| --- | --- |
| Qt 6.11.1 MSVC 2022 x64 | `D:\Develop\envs\Qt\6.11.1\msvc2022_64` |
| GStreamer 1.28.4 开发 SDK | `D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64` |
| CPM 共享源码缓存 | `D:\Develop\envs\qgc-cpm-cache` |
| AI Python，沿用现有环境 | `D:\Develop\envs\yolo\Scripts\python.exe` |
| VS 2022 Build Tools，沿用现有安装 | `D:\Develop\Toolchains\VS2022BuildTools` |
| 构建工具 Python | 项目 `.venv`，Python 3.10.11 |

旧 Qt、GStreamer 和构建目录均保留。新 GStreamer 使用官方安装器的 portable/devel 模式安装，没有注册或修改全局环境。

在 VS 2022 **x64 Native Tools Command Prompt** 中，从项目根目录配置：

```bat
set "PATH=D:\Develop\envs\Qt\6.11.1\msvc2022_64\bin;D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64\bin;D:\Develop\QGC_for_GRobot\.venv\Scripts;%PATH%"
cmake -S . -B build-v5.1.5-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQGC_DEBUG_CANDIDATE=ON -DQGC_BUILD_TESTING=ON -DQGC_BUILD_INSTALLER=OFF -DQGC_USE_CACHE=OFF -DCPM_SOURCE_CACHE=D:/Develop/envs/qgc-cpm-cache -DCMAKE_PREFIX_PATH=D:/Develop/envs/Qt/6.11.1/msvc2022_64 -DGStreamer_ROOT_DIR=D:/Develop/envs/GStreamer/1.28.4/msvc_x86_64 -DPython3_EXECUTABLE=D:/Develop/QGC_for_GRobot/.venv/Scripts/python.exe -DPython_EXECUTABLE=D:/Develop/QGC_for_GRobot/.venv/Scripts/python.exe "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /nologo" "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /nologo"
cmake --build build-v5.1.5-debug --config Debug --parallel 4
```

当前中文 MSVC 与 CMake 4.1.0 曾将 `/showIncludes` 前缀错误解码。此构建目录中的编译器检测元数据已修正为实际输出的 `注意: 包含文件:`（末尾含一个空格），并验证 Ninja 能记录头文件依赖；未修改系统 CMake 或 MSVC。重新创建构建目录时需核对同一问题。

本阶段只执行构建，不执行 `cmake --install`、CPack 或发布脚本。上游安装流程会包含打包步骤，不能仅凭 `QGC_BUILD_INSTALLER=OFF` 推断安装操作安全符合本阶段范围。

## AI 实测配置

AI 页面的 Python 路径填 `D:\Develop\envs\yolo\Scripts\python.exe`，桥接脚本填项目内 `tools\ai_detection\run_yolo_to_qgc_auto.py` 的完整路径；模型选择现有本地模型。
先执行界面中的环境检查，再启动检测。共享 AI 环境与构建工具 `.venv` 用途不同。

独立运行自动 YOLO 入口时，默认优先读取新版 Debug 配置。显式传入
`--settings-file` 时只使用该配置；选定配置没有视频地址时不回退读取其他应用。
`StopDeepSharkQGC.cmd --list` 可先查看将停止的旧版、新 Debug 及相关子进程。

升级遗漏的修复范围和证据见
[迁移修复记录](../audits/upstream_v5.1.5_migration_repairs_2026-10-06.md)。

## 简明实测清单

| 检查 | 需要确认 |
| --- | --- |
| 启动、关闭、重启 | 版本正确，四路视频与状态面板加载，关闭后进程退出，再次打开正常 |
| 飞控连接 | 默认 TCP 端点、参数加载、主动断开与重连正常，未改变正式版设置 |
| 四路视频 | 每路打开、停止、重连；切换网格、主视图、全屏和姿态视图；核对画面比例 |
| RTSP 传输 | 自动/TCP/UDP 选择保存后生效，重启后保留 |
| AI | 环境检查通过，检测启动/停止、标注开关及各路框与画面对应正常 |
| AUV | 姿态、任务状态和飞行模式显示正确，实际飞控操作按原流程确认 |
| 连接告警 | 飞控/视频断联、恢复、确认静音、手动停止/断开不产生错误告警 |
| 手柄 | 先在安全状态核对轴方向、油门零位、死区及普通/Shift 按钮，再进行设备操作 |
| 推进器工具 | 核对映射、参数修改和停止行为；真实电机动作由设备测试验收 |
| 持续运行 | 按原场景长时间运行，记录卡顿、重连、资源增长和语音开关的差异 |

编译、合成视频和离线测试通过不代表真实摄像头、飞控、手柄或长时间运行已验收。稳定后再安排 Release 与安装包。
