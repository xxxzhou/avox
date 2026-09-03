# agent — AI 对话

Phase 3 — VLM (视觉语言模型) 对话客户端。

## 用法

```bash
# 文本对话
avox_cli agent -prompt "描述这张图片" -i photo.jpg

# 交互模式
avox_cli agent -chat

# 指定服务器
avox_cli agent -chat -server http://localhost:1234
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-prompt` | | String | | 提示词 (单次对话) | - |
| `-i` | `--input` | String | | 图片输入 (多模态) | - |
| `-chat` | | Bool | | 交互模式 | 关闭 |
| `-server` | | String | | VLM 服务器地址 | localhost:1234 |

## 对应 SDK API

- `createAgentClient()` → `IAgentClient*`
- `IAgentClient`: `load(serverUrl)`, `chat(text)`, `chatWithImage(text, imagePath)`
- `IAgentClient`: `isServerAvailable()`

## 实现要点

1. 创建 `IAgentClient`，连接服务器
2. 单次模式: `-prompt` 发送请求，输出响应后退出
3. 交互模式: 循环读取 stdin，发送请求，输出响应
4. 图片输入: 使用 `chatWithImage()` 发送多模态请求
5. 支持 LM Studio / OpenAI 兼容的 VLM 服务
