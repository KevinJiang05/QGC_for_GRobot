# QGC_KevinJiang 仓库与目录维护

本项目由单人维护，长期直接在 `main` 开发。上游基线、Kevin 产品版本和实测状态分别记录，避免用分支名表示版本。

## 分支、版本与回退

- `main` 是唯一长期开发分支。需要明确并行任务时才创建短期分支或工作树。
- `upstream` 用于取得官方 QGroundControl 源码；官方标签与历史保持完整。升级使用 Git 合并，并迁移 `custom` 和有明确依据的核心补丁。
- 固定 tag 保存回退节点。已发布 tag 不移动；正式安装包通过 GitHub Release 分发。
- 升级前检查点是 `db9c9159631a271457a7d6c395deb50e2cf3c304`，备份 tag `backup/pre-qgc-v5.1.5` 已创建并推送，可用于完整回退源码。
- 官方合并与定制适配已提交为 `6b044f606de525d7f978d4e64f5bcd26f112ab46` 并推送；GitHub 默认分支与本地开发分支均为 `main`，本地跟踪 `origin/main`。
- 当前源码基于官方 `v5.1.5`（`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`）。当前编译候选仍显示 `1.5.0 Debug (QGroundControl v5.1.5)`；Kevin `2.0.0` 是已讨论的下一代产品编号，尚未修改版本源或重新编译。
- 正式 `1.4.0` 发布与对应 tag 保留。Debug 和预发布 tag 不表示实机验收完成。

## 目录用途

| 位置 | 用途与处理 |
| --- | --- |
| `src/`、`cmake/`、`resources/`、`test/`、`translations/`、`deploy/`、`android/` | 官方工程与必要适配，保留上游结构 |
| `custom/`、`branding/` | Kevin/DeepShark 定制功能和品牌资源 |
| `tools/` | AI 桥接、RTSP、诊断、Debug 启动和正式发布工具 |
| `docs/` | 项目文档、验证记录，以及继承的官方用户文档 |
| `software/` | 已追踪的相关固件和设备工具；本地设备配置已忽略，整理前须核对用途 |
| `build-v5.1.5-debug/` | 当前候选与增量编译目录，已忽略；实测期间保留路径和缓存 |
| `build-debug-ai/`、`build-release/`、`build-mavlink-diagnostics/` | 旧版本构建和诊断产物，已忽略；当前仍有回退用途 |
| `build-v5.1.5-deps/` | 本次升级的工具、下载和检查缓存，已忽略；移动会影响包含绝对路径的环境 |
| `dist/` | 既有安装包和发布审计，已忽略；正式发布物保留 |
| `tmp/`、`.tmp/codex/` | 临时产物与本轮验证证据，已忽略；本轮全部保留，后续清理另行确认 |
| `artifacts/` | 两份社团招新文档，已忽略；当前不纳入 QGC 源码，迁出另行确认 |
| `Ultralytics/`、`.venv/`、工具缓存 | 本地运行设置和开发环境，已忽略 |
| 根目录 `StartDeepSharkQGC.cmd` | 常用双击入口，调用 `tools/debug/start-windows-debug.ps1` 启动新版 |
| `D:\Develop\envs` | 可跨项目复用的 Qt、GStreamer、AI Python 和 CPM 依赖 |

不移动现有 CMake 构建目录来整理外观：缓存、生成规则和部分工具环境含绝对路径。目录已被 Git 忽略，不会随正常源码提交上传。

## 本轮目录盘点与保留

容量是本轮只读盘点值，后续生成文件可能使其变化。

| 位置 | 盘点结果 | 本轮处理 |
| --- | --- | --- |
| `tmp/competition-submission/` | 6700 个文件，约 1584.8 MiB；两份旧比赛源码 staging | 按用户要求保留 |
| `tmp/doc-render/` | 33 个文件，约 9.9 MiB；历史文档渲染产物 | 按用户要求保留 |
| `tmp/video-stutter-2026-10-02/` | 90 个文件，约 6.8 MiB；包含本轮现场视频测量结果 | 保留，仍有诊断价值 |
| 当前及旧构建、`dist/`、固件、共享 SDK、社团文档 | 尚有开发、回退或交付用途 | 本轮保留，未执行移动或删除 |

用户已明确要求“tmp不要删”；本轮未删除或迁移任何目录。此前提出的两处清理候选合计约 1.56 GiB，现均保留；未来批量清理仍须按 AGENTS.md 先说明范围和影响并取得授权。

## 现有 PR 审阅

盘点开始时 GitHub 有 8 个开放 PR，均由 Dependabot 创建。默认分支由 `deepshark/v5.0.8` 重命名为 `main` 后，GitHub 已自动更新这些 PR 的目标分支。升级推送后，按用户授权关闭 #1–#5 并删除对应的 5 个 Dependabot 分支；#6–#8 保留开放。旧检查结果不能证明它们与新版工程兼容。

| PR | 修改 | 新版实际状态 | 处理与后续 |
| --- | --- | --- | --- |
| [#1](https://github.com/KevinJiang05/QGC_for_GRobot/pull/1) | `upload-artifact` v4 → v7 | 新版相关引用已经是 v7，部分旧工作流已移除 | 已关闭，分支已删除 |
| [#2](https://github.com/KevinJiang05/QGC_for_GRobot/pull/2) | `checkout` v4 → v6 | 新版相关引用已经是 v7 | 已关闭，分支已删除，避免回退版本 |
| [#3](https://github.com/KevinJiang05/QGC_for_GRobot/pull/3) | `setup-node` v4 → v6 | 修改的旧 `docs_deploy.yml` 已移除；新版相关引用是 v7 | 已关闭，分支已删除 |
| [#4](https://github.com/KevinJiang05/QGC_for_GRobot/pull/4) | `create-pull-request` v7 → v8 | 修改的旧 `lupdate.yaml` 已移除；新版相关引用是 v8 | 已关闭，分支已删除 |
| [#5](https://github.com/KevinJiang05/QGC_for_GRobot/pull/5) | Apple 签名 Action v5 → v7 | 新版引用已经是 v7 | 已关闭，分支已删除 |
| [#6](https://github.com/KevinJiang05/QGC_for_GRobot/pull/6) | Rollup 4.34.4 → 4.62.4 | 新版锁文件是 4.59.0；旧 PR 仍需刷新基线 | 保留复核，不直接合并旧锁文件 |
| [#7](https://github.com/KevinJiang05/QGC_for_GRobot/pull/7) | nanoid 3.3.8 → 3.3.18 | 新版锁文件是 3.3.16，仍低于已核实修复版本 3.3.18 | 有实际修复价值，刷新基线后验证 |
| [#8](https://github.com/KevinJiang05/QGC_for_GRobot/pull/8) | PostCSS 8.5.1 → 8.5.26 | 新版锁文件是 8.5.23；此 PR 同时更新 nanoid 至 3.3.18 | 刷新基线后验证，可覆盖 #7 的 nanoid 更新 |

本轮没有合并依赖 PR；#6–#8 的锁文件改动均被 GitHub 报告为冲突（`mergeable=false`、`mergeable_state=dirty`），需要基于新版 `main` 刷新后再验证。当前远端仅保留 `main` 和这 3 个依赖分支；已关闭 PR 的历史仍可查阅。

`npm audit --package-lock-only --json` 对当前 v5.1.5 锁文件实际返回退出码 1：5 个受影响包条目（2 high、3 moderate），包含依赖传播计数，不能理解为 5 个独立漏洞。nanoid 有兼容修复；VitePress 1.6.4/Vite/esbuild 依赖链仍有审计项，工具未提供直接的兼容自动修复。它们属于文档工具链，应单独安排验证，不据此改动飞控或 Debug 运行代码。原始结果保存在被忽略的 `.tmp/codex/repository-maintenance/npm-audit-v515.json`。

## 单人维护与 CI

- 允许直接提交到 `main`；保留主分支的禁止强推和禁止删除规则，不强制第二位维护者审批。
- 当前保护规则没有必需 PR 审批、必需状态检查或线性历史要求，适合保留官方合并历史。
- 合并后的短期分支可以删除。关闭 PR 前，核对是否已由新版覆盖，或是否仍包含需要的修复。
- 新版依赖机器人配置已将 GitHub Actions 交给 Dependabot，npm/Python/pre-commit 交给 Renovate。未确认安装或启用 Renovate，不能只凭配置文件假定它正在工作。
- 继承的多项 CI 仍监听 `master`，Windows 工作流默认构建 Release 并包含安装步骤；只改分支名不会使其成为本项目的 Windows Debug 验证流程。
- 后续 CI 适配应围绕 Windows x64 Debug、实际 `custom` 和相关回归展开。保留上游全量 lint 的真实失败记录；不把旧 PR 的绿灯或限定检查当作完整验证。

## 当前 Debug 验证边界

当前程序 SHA256 为 `33a74e82312eeb775be364f4149003d71d5998344e5d908a74982411293598db`，本轮核对与既有验证记录一致。核查开始时，未暂存的实际内容改动仅为双击启动入口、对应 PowerShell 参数和启动说明；此次目录/分支治理不代表新增实机验收。

实测入口和项目验证记录见 [Debug 使用说明](../debug/windows-v5.1.5-debug.md) 与 [升级验证记录](../audits/upstream_v5.1.5_debug_validation_2026-10-05.md)。
