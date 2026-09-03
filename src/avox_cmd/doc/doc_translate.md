# translate — 翻译文本

Phase 2 — 神经机器翻译，支持离线 ONNX 和在线 HTTP 两种引擎。

## 用法

```bash
# 翻译文本
avox_cli translate -text "Hello world" -src en -dst zh

# 翻译文件
avox_cli translate -i input.txt -o output.txt -src en -dst zh

# 使用 ONNX 离线模型
avox_cli translate -text "你好" -src zh -dst en -engine onnx

# 使用在线 API
avox_cli translate -text "Hello" -src en -dst zh -engine http -api tencent
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-text` | | String | | 待翻译文本 (与 -i 二选一) | - |
| `-i` | `--input` | String | | 输入文件 | - |
| `-o` | `--output` | String | | 输出文件 | stdout |
| `-src` | | String | | 源语言 (en/zh/ja/ko/...) | 自动检测 |
| `-dst` | | String | | 目标语言 | zh |
| `-engine` | | String | | 引擎: onnx/http | onnx |
| `-api` | | String | | HTTP API 提供商: tencent | tencent |

## 对应 SDK API

- `createOnnxTranslator()` / `createHttpTranslator()` → `ITranslator*`
- `ITranslator`: `load()`, `setSourceLanguage()`, `setTargetLanguage()`, `translate()`
- `saveTencentApi()`: 保存腾讯云 API 凭证

## 实现要点

1. 根据 `-engine` 创建对应翻译器
2. 设置源/目标语言
3. 调用 `load()` 加载模型
4. 从 `-text` 或 `-i` 文件读取输入
5. 调用 `translate()` 翻译
6. 输出到 `-o` 文件或 stdout
7. 参考: `samples/functest/translationtest.cpp`
