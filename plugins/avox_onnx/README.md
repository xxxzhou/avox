# avox_onnx 模块

通用 ONNX 推理引擎，所有 AI 模块共用。

## 功能
- 加载 ONNX 模型
- CPU/GPU 推理
- 多输入输出支持

## 使用

```cpp
#include "avox_onnx/ONNXRuntime.hpp"

avox_onnx::ONNXSession session;
session.loadModel("model.onnx", false);  // CPU 模式

std::vector<std::pair<std::string, const float*>> inputs = {
    {"input", dataPtr}
};
std::vector<std::vector<float>> outputs;

session.run(inputs, {"output"}, outputs);
```

## CMake 选项

```cmake
option(AVOX_ENABLE_ONNX "build ONNX Runtime support" ON)
```