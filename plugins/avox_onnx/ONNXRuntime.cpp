#include "ONNXRuntime.hpp"

#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <algorithm>

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

ONNXSession::ONNXSession() = default;
ONNXSession::~ONNXSession() { unloadModel(); }

void ONNXSession::unloadModel() {
  session.reset();
  memoryInfo.reset();
  env.reset();
  inputShapes.clear();
  outputShapes.clear();
  inputNames.clear();
  outputNames.clear();
}

// 内部：从内存创建 session
static bool loadModelFromMemoryInternal(std::unique_ptr<Ort::Env>& env,
                                         std::unique_ptr<Ort::Session>& session,
                                         std::unique_ptr<Ort::MemoryInfo>& memoryInfo,
                                         const uint8_t* modelData,
                                         size_t modelSize,
                                         bool useGPU,
                                         int deviceId,
                                         int numThreads,
                                         std::unordered_map<std::string, std::vector<int64_t>>& inputShapes,
                                         std::unordered_map<std::string, std::vector<int64_t>>& outputShapes,
                                         std::vector<std::string>& inputNames,
                                         std::vector<std::string>& outputNames) {
  try {
    env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "avox_onnx");

    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(numThreads);
    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (useGPU) {
      OrtCUDAProviderOptions cudaOptions;
      cudaOptions.device_id = deviceId;
      cudaOptions.arena_extend_strategy = 0;
      cudaOptions.gpu_mem_limit = 2ULL * 1024 * 1024 * 1024;
      cudaOptions.cudnn_conv_algo_search =
          OrtCudnnConvAlgoSearch::OrtCudnnConvAlgoSearchExhaustive;
      cudaOptions.do_copy_in_default_stream = true;
      sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
    }

    session = std::make_unique<Ort::Session>(*env, modelData, modelSize, sessionOptions);

    if (useGPU) {
      memoryInfo = std::make_unique<Ort::MemoryInfo>(
          "Cuda", OrtDeviceAllocator, deviceId, OrtMemTypeDefault);
    } else {
      auto cpuMemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      memoryInfo = std::make_unique<Ort::MemoryInfo>(std::move(cpuMemoryInfo));
    }

    Ort::AllocatorWithDefaultOptions allocator;

    size_t numInputs = session->GetInputCount();
    inputNames.reserve(numInputs);
    for (size_t i = 0; i < numInputs; i++) {
      auto nameAlloc = session->GetInputNameAllocated(i, allocator);
      std::string name = nameAlloc.get();
      inputNames.push_back(name);
      auto typeInfo = session->GetInputTypeInfo(i);
      auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      inputShapes[name] = tensorInfo.GetShape();
    }

    size_t numOutputs = session->GetOutputCount();
    outputNames.reserve(numOutputs);
    for (size_t i = 0; i < numOutputs; i++) {
      auto nameAlloc = session->GetOutputNameAllocated(i, allocator);
      std::string name = nameAlloc.get();
      outputNames.push_back(name);
      auto typeInfo = session->GetOutputTypeInfo(i);
      auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      outputShapes[name] = tensorInfo.GetShape();
    }

    return true;

  } catch (const Ort::Exception& e) {
    std::cerr << "[ONNXSession] Error loading model from memory: " << e.what() << std::endl;
    return false;
  }
}

// 内部：从文件路径创建 session
static bool loadModelFromPathInternal(std::unique_ptr<Ort::Env>& env,
                                       std::unique_ptr<Ort::Session>& session,
                                       std::unique_ptr<Ort::MemoryInfo>& memoryInfo,
                                       const std::string& modelPath,
                                       bool useGPU,
                                       int deviceId,
                                       int numThreads,
                                       std::unordered_map<std::string, std::vector<int64_t>>& inputShapes,
                                       std::unordered_map<std::string, std::vector<int64_t>>& outputShapes,
                                       std::vector<std::string>& inputNames,
                                       std::vector<std::string>& outputNames) {
  try {
    env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "avox_onnx");

    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(numThreads);
    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (useGPU) {
      OrtCUDAProviderOptions cudaOptions;
      cudaOptions.device_id = deviceId;
      cudaOptions.arena_extend_strategy = 0;
      cudaOptions.gpu_mem_limit = 2ULL * 1024 * 1024 * 1024;
      cudaOptions.cudnn_conv_algo_search =
          OrtCudnnConvAlgoSearch::OrtCudnnConvAlgoSearchExhaustive;
      cudaOptions.do_copy_in_default_stream = true;
      sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
    }

#ifdef _WIN32
    std::wstring wModelPath = utf8TWstring(modelPath);
    session = std::make_unique<Ort::Session>(*env, wModelPath.c_str(), sessionOptions);
#else
    session = std::make_unique<Ort::Session>(*env, modelPath.c_str(), sessionOptions);
#endif

    if (useGPU) {
      memoryInfo = std::make_unique<Ort::MemoryInfo>(
          "Cuda", OrtDeviceAllocator, deviceId, OrtMemTypeDefault);
    } else {
      auto cpuMemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      memoryInfo = std::make_unique<Ort::MemoryInfo>(std::move(cpuMemoryInfo));
    }

    Ort::AllocatorWithDefaultOptions allocator;

    size_t numInputs = session->GetInputCount();
    inputNames.reserve(numInputs);
    for (size_t i = 0; i < numInputs; i++) {
      auto nameAlloc = session->GetInputNameAllocated(i, allocator);
      std::string name = nameAlloc.get();
      inputNames.push_back(name);
      auto typeInfo = session->GetInputTypeInfo(i);
      auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      inputShapes[name] = tensorInfo.GetShape();
    }

    size_t numOutputs = session->GetOutputCount();
    outputNames.reserve(numOutputs);
    for (size_t i = 0; i < numOutputs; i++) {
      auto nameAlloc = session->GetOutputNameAllocated(i, allocator);
      std::string name = nameAlloc.get();
      outputNames.push_back(name);
      auto typeInfo = session->GetOutputTypeInfo(i);
      auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      outputShapes[name] = tensorInfo.GetShape();
    }

    return true;

  } catch (const Ort::Exception& e) {
    std::cerr << "[ONNXSession] Error loading model: " << e.what() << std::endl;
    return false;
  }
}

bool ONNXSession::loadModel(const std::string& assetPath,
                             bool useGPU,
                             int deviceId,
                             int numThreads) {
  unloadModel();

  // 1. 先尝试用 AssetLoader 加载 (支持 Android assets)
  auto modelData = AssetLoader::loadToMemory(assetPath.c_str());
  if (!modelData.empty()) {
    LOGFLF(LogLevel::info,"load asset:",assetPath);
    if (loadModelFromMemoryInternal(env, session, memoryInfo,
                                    modelData.data(), modelData.size(),
                                    useGPU, deviceId, numThreads,
                                    inputShapes, outputShapes,
                                    inputNames, outputNames)) {
      return true;
    }
    unloadModel();
  }
  // 2. Fallback: 作为文件路径加载
  LOGFLF(LogLevel::info,"load asset:",assetPath);
  if (loadModelFromPathInternal(env, session, memoryInfo,
                                assetPath, useGPU, deviceId, numThreads,
                                inputShapes, outputShapes,
                                inputNames, outputNames)) {
    return true;
  }

  // 3. 如果 GPU 模式失败，自动切换为 CPU 重试
  if (useGPU) {
    LOGFLF(LogLevel::info, "GPU mode failed, retrying with CPU...");
    unloadModel();
    if (loadModelFromPathInternal(env, session, memoryInfo,
                                  assetPath, false, deviceId, numThreads,
                                  inputShapes, outputShapes,
                                  inputNames, outputNames)) {
      LOGFLF(LogLevel::info, "Switched to CPU mode successfully");
      return true;
    }
  }

  unloadModel();
  return false;
}

std::vector<int64_t> ONNXSession::getInputShape(const std::string& name) const {
  auto it = inputShapes.find(name);
  if (it != inputShapes.end()) {
    return it->second;
  }
  return {};
}

std::vector<int64_t> ONNXSession::getOutputShape(const std::string& name) const {
  auto it = outputShapes.find(name);
  if (it != outputShapes.end()) {
    return it->second;
  }
  return {};
}

size_t ONNXSession::getInputSize(const std::string& name) const {
  auto shape = getInputShape(name);
  size_t size = 1;
  for (int64_t dim : shape) {
    size *= static_cast<size_t>(dim);
  }
  return size;
}

std::vector<std::string> ONNXSession::getInputNames() const {
  return inputNames;
}

std::vector<std::string> ONNXSession::getOutputNames() const {
  return outputNames;
}

// 读取模型自定义元数据 (Ultralytics 的 names/task/imgsz 等)
std::unordered_map<std::string, std::string> ONNXSession::getCustomMetadata() const {
  std::unordered_map<std::string, std::string> out;
  if (!session) return out;
  try {
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::ModelMetadata meta = session->GetModelMetadata();
    // 仅取通用模型需要的 key, 不必枚举整个 map
    for (const char* key : {"names", "task", "imgsz"}) {
      auto v = meta.LookupCustomMetadataMapAllocated(key, allocator);
      if (v) out[key] = v.get();
    }
  } catch (const Ort::Exception& e) {
    std::cerr << "[ONNXSession] getCustomMetadata error: " << e.what() << std::endl;
  }
  return out;
}

// 推理 - float 版本 (用于 CV 模型，固定形状)
bool ONNXSession::run(const std::vector<std::pair<std::string, const float*>>& inputs,
                      const std::vector<std::string>& outputNames,
                      std::vector<std::vector<float>>& outputs) {
  if (!session) {
    std::cerr << "[ONNXSession] Session not loaded" << std::endl;
    return false;
  }

  try {
    std::vector<Ort::Value> inputTensors;
    inputTensors.reserve(inputs.size());

    for (const auto& input : inputs) {
      auto it = inputShapes.find(input.first);
      if (it == inputShapes.end()) {
        std::cerr << "[ONNXSession] Input not found: " << input.first << std::endl;
        return false;
      }

      std::vector<int64_t> shape = it->second;
      // 动态维度设为 1
      for (auto& dim : shape) {
        if (dim <= 0) dim = 1;
      }
      if (!shape.empty()) shape[0] = 1;  // batch=1

      size_t size = 1;
      for (int64_t dim : shape) size *= static_cast<size_t>(dim);

      inputTensors.push_back(Ort::Value::CreateTensor<float>(
          *memoryInfo, const_cast<float*>(input.second), size,
          shape.data(), shape.size()));
    }

    // 准备输出
    std::vector<Ort::Value> outputTensors;
    outputs.resize(outputNames.size());
    for (size_t i = 0; i < outputNames.size(); i++) {
      auto it = outputShapes.find(outputNames[i]);
      if (it == outputShapes.end()) {
        std::cerr << "[ONNXSession] Output not found: " << outputNames[i] << std::endl;
        return false;
      }

      std::vector<int64_t> shape = it->second;
      for (auto& dim : shape) if (dim <= 0) dim = 1;

      size_t size = 1;
      for (int64_t dim : shape) size *= static_cast<size_t>(dim);
      outputs[i].resize(size);

      outputTensors.push_back(Ort::Value::CreateTensor<float>(
          *memoryInfo, outputs[i].data(), size,
          shape.data(), shape.size()));
    }

    // 运行推理
    std::vector<const char*> inputNamesPtr;
    for (const auto& input : inputs) inputNamesPtr.push_back(input.first.c_str());

    std::vector<const char*> outputNamesPtr;
    for (const auto& name : outputNames) outputNamesPtr.push_back(name.c_str());

    session->Run(Ort::RunOptions{nullptr},
                 inputNamesPtr.data(), inputTensors.data(), inputTensors.size(),
                 outputNamesPtr.data(), outputTensors.data(), outputTensors.size());

    return true;

  } catch (const Ort::Exception& e) {
    std::cerr << "[ONNXSession] Inference error: " << e.what() << std::endl;
    return false;
  }
}

// 推理 - 统一版本，支持动态形状自动推导
bool ONNXSession::runAuto(const std::vector<std::tuple<std::string, const void*, size_t, bool>>& allInputs,
                          const std::vector<std::string>& outputNames,
                          std::vector<std::vector<float>>& outputs) {
  if (!session) {
    std::cerr << "[ONNXSession] Session not loaded" << std::endl;
    return false;
  }

  try {
    std::vector<Ort::Value> inputTensors;
    std::vector<const char*> inputNamesPtr;

    for (const auto& [name, data, count, isFloat] : allInputs) {
      auto it = inputShapes.find(name);
      if (it == inputShapes.end()) continue;

      // --- 动态 Shape 自动推导逻辑 ---
      std::vector<int64_t> actualShape = it->second;
      int64_t constantPart = 1;
      std::vector<int> dynamicIdxs;

      for (int i = 0; i < static_cast<int>(actualShape.size()); ++i) {
        if (actualShape[i] <= 0) {
          dynamicIdxs.push_back(i);
        } else {
          constantPart *= actualShape[i];
        }
      }

      // 处理动态维度
      if (!dynamicIdxs.empty()) {
        int64_t dynamicProduct = static_cast<int64_t>(count) / constantPart;
        if (dynamicIdxs.size() == 1) {
          actualShape[dynamicIdxs[0]] = dynamicProduct;
        } else if (dynamicIdxs.size() == 2) {
          // 假设第一个动态维度是 batch=1
          actualShape[dynamicIdxs[0]] = 1;
          actualShape[dynamicIdxs[1]] = dynamicProduct;
        } else {
          for (int idx : dynamicIdxs) actualShape[idx] = 1;
          actualShape[dynamicIdxs.back()] = dynamicProduct;
        }
      }

      inputNamesPtr.push_back(name.c_str());
      if (isFloat) {
        inputTensors.push_back(Ort::Value::CreateTensor<float>(
            *memoryInfo, const_cast<float*>(static_cast<const float*>(data)), count,
            actualShape.data(), actualShape.size()));
      } else {
        inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
            *memoryInfo, const_cast<int64_t*>(static_cast<const int64_t*>(data)), count,
            actualShape.data(), actualShape.size()));
      }
    }

    // 运行推理：让 ORT 分配输出
    std::vector<const char*> outNamesPtr;
    for (const auto& n : outputNames) outNamesPtr.push_back(n.c_str());

    auto outputValues = session->Run(Ort::RunOptions{nullptr},
                                     inputNamesPtr.data(), inputTensors.data(), inputTensors.size(),
                                     outNamesPtr.data(), outNamesPtr.size());

    // 拷贝结果
    outputs.clear();
    for (auto& val : outputValues) {
      auto info = val.GetTensorTypeAndShapeInfo();
      float* rawData = val.GetTensorMutableData<float>();
      outputs.emplace_back(rawData, rawData + info.GetElementCount());
    }
    return true;

  } catch (const std::exception& e) {
    std::cerr << "[ONNXSession] runAuto error: " << e.what() << std::endl;
    return false;
  }
}

// 推理 - 显式指定输入 shape (多空间动态维度模型用, 如 OCR det [1,3,H,W])
bool ONNXSession::runShaped(const std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>>& inputs,
                             const std::vector<std::string>& outputNames,
                             std::vector<std::vector<float>>& outputs) {
  if (!session) {
    std::cerr << "[ONNXSession] Session not loaded" << std::endl;
    return false;
  }
  try {
    std::vector<Ort::Value> inputTensors;
    std::vector<const char*> inputNamesPtr;
    for (const auto& [name, data, shape] : inputs) {
      size_t size = 1;
      for (int64_t dim : shape) size *= static_cast<size_t>(dim);
      inputNamesPtr.push_back(name.c_str());
      inputTensors.push_back(Ort::Value::CreateTensor<float>(
          *memoryInfo, const_cast<float*>(data), size,
          shape.data(), shape.size()));
    }
    // 输出由 ORT 自动分配 (动态输出维度)
    std::vector<const char*> outNamesPtr;
    for (const auto& n : outputNames) outNamesPtr.push_back(n.c_str());
    auto outputValues = session->Run(Ort::RunOptions{nullptr},
                                     inputNamesPtr.data(), inputTensors.data(), inputTensors.size(),
                                     outNamesPtr.data(), outNamesPtr.size());
    outputs.clear();
    for (auto& val : outputValues) {
      auto info = val.GetTensorTypeAndShapeInfo();
      float* rawData = val.GetTensorMutableData<float>();
      outputs.emplace_back(rawData, rawData + info.GetElementCount());
    }
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[ONNXSession] runShaped error: " << e.what() << std::endl;
    return false;
  }
}

}
