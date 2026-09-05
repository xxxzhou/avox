# UE 插件 (AvoxPlayer) — 已迁移至 avox-ue 仓

AvoxPlayer 插件的源码、演示工程、打包工具与文档已整体迁移至独立分发仓:

- **avox-ue** (私有): `D:\Work\github\avox-ue` → github.com/xxxzhou/avox-ue

包含 (2026-09-05 迁移时的全部修复):
- 插件源码: avox.dll 全路径加载修复 / glsl 着色器部署 / 硬解+离屏无帧修复
  (删 setVulkan(false)) / bAllowSoftFallback 软解回退 / AAvoxMediaPlayerActor
- demo 工程 AvoxUEDemo (UE 5.8.2 四组合验证通过: H.264/HEVC × 软/硬解)
- 工具: deploy_plugin.ps1 / build_demo.ps1 / stage_plugin.ps1 / run_buildplugin.cmd /
  check_licenses.py (faad/fdk 自检) / run_verify.py (UE Python 远程执行验证)
- 文档: quickstart / player / packaging (Fab 提交) / troubleshooting (踩坑实录)

本目录不再维护, 改动请提交到 avox-ue 仓。
