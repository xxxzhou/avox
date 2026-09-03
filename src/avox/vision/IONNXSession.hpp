#pragma once

#include <string>
#include <vector>
#include <memory>
#include <tuple>
#include <unordered_map>
#include "avox/AvoxDef.h"

namespace avox {

// ============== ONNX 推理引擎 ==============

class IONNXSession {
 public:
  virtual ~IONNXSession() = default;

  // 加载模型
  virtual bool loadModel(const std::string& modelPath,
                        bool useGPU = true,
                        int deviceId = 0,
                        int numThreads = 4) = 0;

  // 卸载模型
  virtual void unloadModel() = 0;

  // 检查状态
  virtual bool isLoaded() const = 0;

  // 获取输入输出信息
  virtual std::vector<int64_t> getInputShape(const std::string& name) const = 0;
  virtual std::vector<int64_t> getOutputShape(const std::string& name) const = 0;
  virtual size_t getInputSize(const std::string& name) const = 0;
  virtual std::vector<std::string> getInputNames() const = 0;
  virtual std::vector<std::string> getOutputNames() const = 0;

  // 自定义元数据 (Ultralytics 的 names/task/imgsz 等); 未加载返回空 map
  virtual std::unordered_map<std::string, std::string> getCustomMetadata() const = 0;

  // 推理 - float 版本 (用于 CV 模型，固定形状)
  virtual bool run(const std::vector<std::pair<std::string, const float*>>& inputs,
                  const std::vector<std::string>& outputNames,
                  std::vector<std::vector<float>>& outputs) = 0;

  // 推理 - 统一版本，支持动态形状自动推导
  // 参数: {name, data, count, isFloat} - isFloat=true 表示 float*, false 表示 int64_t*
  // 注: 仅 1 个动态维度时正确; 多空间动态维度(如 OCR det [1,3,H,W])用 runShaped 显式指定
  virtual bool runAuto(const std::vector<std::tuple<std::string, const void*, size_t, bool>>& inputs,
                       const std::vector<std::string>& outputNames,
                       std::vector<std::vector<float>>& outputs) = 0;

  // 推理 - 显式指定输入 shape (用于多空间动态维度模型, 如 OCR det [1,3,H,W])
  // inputs: {name, float* 数据, 完整 shape(含 batch)}; 输出由 ORT 自动分配
  virtual bool runShaped(const std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>>& inputs,
                         const std::vector<std::string>& outputNames,
                         std::vector<std::vector<float>>& outputs) = 0;
};

}
