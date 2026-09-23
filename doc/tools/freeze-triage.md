# 冻结报告承接指南(维护者)

> 状态: 有效 · 上次核对: 2026-09-24 · 权威源: -

用户经 Issues「冻结/无响应报告」模板提交冻结名片(纯文本)与可选 mini dump
(~80KB)。本文是拿到证据后的分析路数。

## 名片怎么读

每行一个线程: `[card] tid=... rip=<模块>+<偏移> sym=<符号>+<偏移>`。

- 找 **非 ntdll 等待** 的线程: 正常闲置的线程大多停在 `ntdll.dll+0x16xxxx`
  (WaitForAlertByThreadId 等待族); `win32u.dll` 是消息泵(GetMessage 类)。
- 楔死锚点通常是停在 **引擎(avox)或 vulkan-1 加载器内部** 的那一条。
- 平台线程泡 GetMessage 属正常——冻结的是帧生产/呈现, 不是消息泵。

## dump 怎么分析

工具: panvox 仓 `tools/analyze_hang.py`(依赖 `pip install minidump pefile`)。

```bash
python tools/analyze_hang.py <用户dump> <可疑tid>   # tid 取自名片
```

- 按 PE .pdata 校验栈帧(比栈扫描可靠), 无需 cdb;
- 符号化需要与**引擎提交**匹配的 `avox.pdb`(install 目录里随构建产出,
  部署副本无调试目录——分析必须用 install 的 dll+pdb 对);
- 3GB 级 full dump 与 80KB mini dump 同一脚本通吃(minidump 库两种流都认)。

## 复现

测试宿主 avox-test `l1_avox/wedge`(vulkan_wedge_repro): K 线程反复
open/playing/close 叠加裸枚举扫描线程, 停摆 30s 自动落名片+mini dump。

```bash
vulkan_wedge_repro --file=<mp4> --sessions=4 --iters=2000 \
  --vulkan-render=1 --stall-seconds=30 --out=out/wedge
```

已知案例(2026-09-20): 渐进扫墙+开媒体冻结, 锚点 = avox
`VKLayerProps::init()` 一族 → vulkan-1 加载器内部; 本机第三方隐式层 7 个
在册, 二分嫌疑最大。详见 panvox 仓 `docs/reports/freeze-20260920-progressive-scan.md`。
