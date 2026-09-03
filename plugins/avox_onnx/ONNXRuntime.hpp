#pragma once

#include "avox/vision/IONNXSession.hpp"
#include <unordered_map>

// 前向声明 ONNX Runtime 类型
namespace Ort {
  class Env;
  class Session;
  class MemoryInfo;
}

namespace avox {

// ============== ONNX Runtime 实现 ==============

class ONNXSession : public IONNXSession {
 public:
  ONNXSession();
  ~ONNXSession();

  // 禁止拷贝
  ONNXSession(const ONNXSession&) = delete;
  ONNXSession& operator=(const ONNXSession&) = delete;

  // 加载模型 (自动处理 asset 路径和文件路径)
  bool loadModel(const std::string& assetPath,
                 bool useGPU = true,
                 int deviceId = 0,
                 int numThreads = 4) override;

  void unloadModel() override;
  bool isLoaded() const override { return session != nullptr; }

  // 获取输入输出信息
  std::vector<int64_t> getInputShape(const std::string& name) const override;
  std::vector<int64_t> getOutputShape(const std::string& name) const override;
  size_t getInputSize(const std::string& name) const override;
  std::vector<std::string> getInputNames() const override;
  std::vector<std::string> getOutputNames() const override;
  std::unordered_map<std::string, std::string> getCustomMetadata() const override;

  // 推理 - float 版本 (用于 CV 模型，固定形状)
  bool run(const std::vector<std::pair<std::string, const float*>>& inputs,
           const std::vector<std::string>& outputNames,
           std::vector<std::vector<float>>& outputs) override;

  // 推理 - 统一版本，支持动态形状自动推导
  bool runAuto(const std::vector<std::tuple<std::string, const void*, size_t, bool>>& inputs,
               const std::vector<std::string>& outputNames,
               std::vector<std::vector<float>>& outputs) override;

  // 推理 - 显式指定输入 shape (多空间动态维度模型用)
  bool runShaped(const std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>>& inputs,
                 const std::vector<std::string>& outputNames,
                 std::vector<std::vector<float>>& outputs) override;

 private:
  std::unique_ptr<Ort::Env> env;
  std::unique_ptr<Ort::Session> session;
  std::unique_ptr<Ort::MemoryInfo> memoryInfo;

  std::unordered_map<std::string, std::vector<int64_t>> inputShapes;
  std::unordered_map<std::string, std::vector<int64_t>> outputShapes;
  std::vector<std::string> inputNames;
  std::vector<std::string> outputNames;
};

}
