# CI C++ 编译检查标准

## 编译成功判断逻辑

`build_common.py` 通过以下方式综合判断构建成功/失败：

1. **return_code**：非 0 表示失败
2. **输出内容关键词**：`构建失败`、`error LNK`、`fatal error`、`error C`
3. **构建产物存在性**：通过 CI workflow 中的 `Verify C++ SDK` 步骤检查

## 失败时错误提取

构建失败时，`build_common.py` 会自动提取关键错误信息并输出到日志，包括：

| 平台 | 错误关键词 |
|------|-----------|
| Windows | `error LNK`、`fatal error`、`error C`/`error D`、`unresolved external`/`无法解析的外部符号` |
| Android | `clang++: error`、`ninja: build stopped`、`error:`、`FAILED:` |

提取结果以 `=====` 分隔线包裹，每行一个错误，最多显示 10 条。

## CI 工作流要求

- `python build_windows.py` / `python build_android.py` 必须 `exit 0` 才能继续后续步骤
- `Run unit tests (Windows)` 步骤执行 `ctest`（单元测试随构建自动编译，`AVOX_BUILD_TESTS=ON` 默认开启），失败即整体失败
- 后续 .NET 编译依赖于 C++ SDK 成功构建，必须等 C++ 完成
- `Verify C++ SDK (Windows)` 检查 `avox.dll` + `AvoxWrapper.dll` 是否存在
- `Verify C++ SDK (Android)` 检查 `libavox.so` 是否存在
- dotnet build/restore 步骤添加 `|| exit 1` 确保失败时立即停止

## 本地构建验证

本地构建时，同样检查以上标准。如果 `构建失败` 或链接错误出现，说明 C++ 编译有问题，需要修复后才能继续。
