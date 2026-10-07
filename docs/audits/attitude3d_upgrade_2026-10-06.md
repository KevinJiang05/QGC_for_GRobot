# 3D 姿态驾驶视图升级（2026-10-06）

默认从机尾上方观察，镜头以惯性方式随航向转动，转弯时露出机体侧面，停止转向后平稳回到机尾。镜头保持世界竖直方向，因此机体横滚和俯仰不会带着镜头倾斜。沿用原来的简化模型和姿态方向映射，原有即时跟随保留为“硬跟随”。

## 操作

- 视频主界面默认在原位置显示 3D 小窗；收起状态栏后小窗仍显示。最小化 DeepShark 面板或切换地图主界面时沿用整体面板的隐藏行为。
- 小窗可选择惯性跟随、硬跟随、固定视角、俯视、左侧、右侧、前视、自由观察。
- 点击“放大”或双击小窗进入 3D 工作区；点击工作区“视频”返回。两处共享视角和缩放状态。
- 放大后拖动进入自由观察，滚轮缩放；双击或“回到跟随”恢复默认视角。
- 航向、横滚、俯仰、深度和连接状态在小窗及大视图中显示；缺少有效数据时显示等待状态或“—”。

## 深度与水体

用户确认：水面附近遥测约为 0，下潜为负数，常用作业范围为 0–1.5 米。直接复用 `Vehicle.altitudeRelative` Fact，显示深度为其负值。例如遥测 -1.5 显示深度 1.50 m；正的相对高度保留为负深度，表示高于水面，不取绝对值。

- 水面固定在世界高度 0，模型随深度上下移动，镜头跟随其高度。
- 水面采用淡色半透明材质与轻微动态波纹，配合蓝绿色背景、远距离雾效及水面网格；水面和网格不向模型投影。
- 右侧深度刻度默认 0–2 米，超过范围时按 0.5 米增量扩展；青色线标示当前深度。
- 简化模型约 330 场景单位长，显示比例设为 300 单位/米，即约 1.1 米长。这是可视化比例，未按实物 CAD 标定。
- 深度失效或通信中断时保留最后有效高度，隐藏刻度上的当前值标记，文本显示“深度 —”；切换载具或断开载具时清除旧深度。

水体与网格提供水面和姿态参考，没有海底、障碍物、真实水色或水平航迹含义。水面零点依赖现有遥测的零点，不在此视图自动校零。

## 实现范围

- `custom/src/DeepShark/Attitude3DPanel.qml`：将航向父节点与俯仰/横滚子节点分开，硬跟随复用同一个航向动画，惯性跟随由 `FrameAnimation` 按实际帧间隔驱动。坐标约定保持原模型的前方 -Z、右方 +X、上方 +Y。隐藏、无数据或通信中断时停止持续动画。
- `custom/src/DeepShark/Attitude3DViewState.qml`：保留视角、观察角度、缩放、深度和惯性镜头状态，避免 Loader 重建时丢失状态。惯性使用临界阻尼解析更新，角差走最短路径；大视图角度滞后上限 30°，小窗 20°，高度滞后上限约 0.1 米。新载具、模式切换或长时间暂停后重新初始化。
- `custom/src/DeepShark/WaterSurface.frag`：水面波纹着色，无额外液体模拟依赖。
- `custom/src/FlyViewCustomLayer.qml`、`custom/src/DeepShark/FourVideoPanel.qml`：接入默认小窗、放大入口和共享状态；资源登记在 `custom/custom.qrc`。
- `test/DeepShark/Attitude3DPanelTest.*`、`test/DeepShark/CMakeLists.txt`：姿态数学和实际图形界面验证。

## 验证结果

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Debug 增量构建 | 通过 | `build-v5.1.5-debug/attitude3d-build.log` |
| 姿态、惯性和深度定向测试 | `Attitude3DPanelTest` 通过，5.41 秒 | `build-v5.1.5-debug/attitude3d-focused-tests.log` |
| Windows 实际图形界面 | 通过，无失败或跳过，16.42 秒 | `.tmp/codex/attitude3d-ui/native-ui-results-Attitude3DUITest.xml` |
| 针对修改文件的 pre-commit 检查 | 11 个适用检查通过 | `build-v5.1.5-debug/attitude3d-lint-results.json` |
| QML 静态检查 | 两个姿态组件退出码 0，无警告 | `.tmp/codex/attitude3d-qmllint.log` |

姿态测试覆盖组合横滚/俯仰、接近竖直姿态、359°→1°航向过渡、镜头保持水平、固定视角、自由观察、状态共享和连接中断。新增惯性转向/停止回正、30/120 fps 下相同固定目标的结果一致、深度符号、水面零点、无效深度及切换载具检查。原生界面测试以实际帧动画输入连续转向，检查角度滞后及停止后回正，检查硬跟随、新旧视角、0/1.5 米深度、默认小窗、放大/返回位置、拖动、双击恢复和状态栏收起。

复测曾因 Qt 3D 节点浮点动画的末值与目标相差约 0.0044 单位导致精确相等断言失败；显示误差约 0.015 mm。节点位置断言改为小于 0.05 单位的容差，原始 Fact 换算和状态值仍要求精确相等。失败记录保留在 `.tmp/codex/attitude3d-ui/animation-precision-failure.xml`，最终定向及原生图形复测通过。

Qt Quick 的 software 后端不支持 Qt Quick 3D，图形用例在该后端明确跳过；实际渲染以 Windows 原生图形运行结果为准。截图使用注入的姿态/深度数据，尚未验证真实飞控、视频同步、持续渲染性能和镜头驾驶手感。本次只运行 3D 相关测试，没有全量测试，Claude 独立复核按用户要求暂停。

## 预览

- `.tmp/codex/attitude3d-ui/attitude3d-preview.png`：视频主界面及原位置小窗。
- `.tmp/codex/attitude3d-ui/attitude3d-preview-compact-status.png`：收起状态栏后的小窗。
- `.tmp/codex/attitude3d-ui/attitude3d-view-0.png`：惯性跟随静止大视图。
- `.tmp/codex/attitude3d-ui/attitude3d-view-7.png`：硬跟随。
- `.tmp/codex/attitude3d-ui/attitude3d-inertial-turn.png`：连续转弯时露出机体侧面。
- `.tmp/codex/attitude3d-ui/attitude3d-depth-0.png`、`attitude3d-depth-1.5.png`：水面零点及 1.5 米下潜。
- `.tmp/codex/attitude3d-ui/attitude3d-view-2.png`：俯视。
- `.tmp/codex/attitude3d-ui/attitude3d-view-4.png`：右侧视角。
- `.tmp/codex/attitude3d-ui/attitude3d-view-1.png`：固定视角。
- `.tmp/codex/attitude3d-ui/attitude3d-steep-pitch.png`：大角度俯仰。
