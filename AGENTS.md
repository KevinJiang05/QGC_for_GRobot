# Codex 项目协作说明

## Windows 发布与打包

- 准备 Windows 安装包时，优先使用 `tools/release/build-windows-release.ps1 -Version <版本号>`，避免每次重新推导或手工拼接构建命令。
- Codex 主要负责监督发布过程：检查 `release-state.json`、各步骤日志和最终 `release-report.md`，确认版本、依赖、安装包哈希及覆盖升级契约。
- 若脚本不可用或执行失败，先参考 `docs/releases/windows-release-runbook.md` 和对应步骤日志定位原因；确有必要时再采用手工命令，并说明与标准流程的差异。
- 最终报告未显示成功或关键校验未通过时，应明确提示风险，不建议分发该安装包。
