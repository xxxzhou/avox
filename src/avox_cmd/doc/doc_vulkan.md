# vulkan — Vulkan 信息

Phase 3 — 查询 Vulkan GPU 可用性和设备信息。

## 用法

```bash
# 检查 Vulkan 是否可用
avox_cli vulkan check

# 详细信息
avox_cli vulkan info
```

## 输出示例

**check:**
```
Vulkan: available
```

**info:**
```
Vulkan: available
  GPU: NVIDIA GeForce RTX 3060
  API: 1.3
  Memory: 12288 MB
```

## 对应 SDK API

- `canVulkan()`: 检查 Vulkan 是否可用
- `avox_vulkan` 模块: GPU 设备信息查询

## 实现要点

1. `check`: 调用 `canVulkan()`，输出 available/unavailable
2. `info`: 进一步查询 GPU 名称、API 版本、显存大小
3. 退出码: 0=可用, 1=不可用 (方便脚本判断)
