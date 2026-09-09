# VSCode 项目卡顿排查

> 适用:avox / avox 这类大型 C++ 项目,在 VSCode 里开发一两个月后出现"所有操作都卡"。

## 症状

- 打字 / 悬停 / 保存、鼠标移动、切窗口、打开文件、搜索、终端,**全部顿**
- 非常隐蔽:Task Manager 里 CPU 几乎显示 0(实际是 renderer 烧满一核,但 20 线程下只显示 ~3%),磁盘 / 内存 / GPU 全部正常
- **重拉 clone 或改文件夹名"暂时有效",但一两个月后复发**

## 根因

VSCode 按"项目文件夹路径"累积的工作区缓存(`%APPDATA%\Code\User\workspaceStorage\`),主要成分:

- 窗口布局(`state.vscdb`:打开的标签、面板、编辑器分组)—— 每次打开项目都恢复这套布局
- 各扩展按路径存的会话/状态(Copilot 聊天记录、js-debug 等)
- 大文件标签长期开着(如 2.9MB 的 swig 生成 `.cxx`)→ 渲染器(renderer)界面线程满负荷,renderer 每 5s 烧 1.5~2.5s CPU

**重拉 clone / 改文件夹名 = 路径变 = 全新工作区缓存 = 暂时顺。仓库内容、build、cpptools 缓存都不是根因。**

## 判定实验(可选,用于确认)

1. 清空所有缓存 / build / cpptools 后仍卡 → 排除那些方向
2. 把项目文件夹改名(如 `avox` → `avox1`)→ 立刻不卡 → 实锤是**路径绑定的工作区状态**
   - ⚠️ 改名会弄坏 CMakeCache、`.vscode/launch.json` 等写死路径的引用,**别用改名当解法**,只当判定手段

## 有效解法(关掉 VSCode 后执行)

删除这三个目录(纯缓存,自动重建;只丢窗口布局/恢复标签,不影响代码和配置):

```
%APPDATA%\Code\Cache
%APPDATA%\Code\CachedData
%APPDATA%\Code\User\workspaceStorage
```

重新打开项目 → 全新建空窗口,不卡。**比重拉 clone、比改名都干净。**

## 日常预防

- 别让巨大的生成文件(如 `commonJAVASCRIPT_wrap.cxx` 这类几 MB 的 swig wrapper)长期开在编辑器里
- AI 面板(Copilot / marscode / trae)用完就关,别常驻
- 相关:cpptools 全局缓存 `%LOCALAPPDATA%\Microsoft\vscode-cpptools` 也能长到 8GB,卡时可一并清理

## 排查手法(下次遇到可复用)

- 进程 CPU 采样:PowerShell 3s 窗口 `Get-Process` CPU 增量 >1s 即为峰值
- **Git Bash 的 `du` 在 Windows 不可靠**(曾虚报 20G),体积一律用 PowerShell 查
- GPU 占用用 `\GPU Engine(*)\Utilization Percentage`
