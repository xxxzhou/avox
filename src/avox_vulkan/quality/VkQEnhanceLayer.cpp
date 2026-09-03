#include "VkQEnhanceLayer.hpp"

#include <chrono>

#include "../layer/VkPipeGraph.hpp"
#include "../vulkan/VkCommand.hpp"
#include "../vulkan/VkLayout.hpp"
#include "../VkHelper.hpp"
#include "avox/vision/OnnxModelUser.hpp"
#include "avox/Avox.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

// postprocess push constant: srcWidth, srcHeight, dstWidth, dstHeight
struct PostprocessPushConstant {
  int32_t srcWidth;
  int32_t srcHeight;
  int32_t dstWidth;
  int32_t dstHeight;
};

VkQEnhanceLayer::VkQEnhanceLayer() {
  glslPath = "glsl/quality_preprocess.comp.spv";
  inCount = 1;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
  taskName = "quality infer";
}

VkQEnhanceLayer::~VkQEnhanceLayer() {
  stopTask();
  if (postprocessPipeline) {
    vkDestroyPipeline(vkDevice, postprocessPipeline, nullptr);
  }
  if (postprocessStageInfo.module) {
    vkDestroyShaderModule(vkDevice, postprocessStageInfo.module, nullptr);
  }
}

void VkQEnhanceLayer::calcOutputSize() {
  int32_t srcW = inFormats[0].width;
  int32_t srcH = inFormats[0].height;
  QualityOutputMode mode = paramet.outputMode;
  if (mode == QualityOutputMode::Auto) {
    mode = (srcW >= 1920 || srcH >= 1080) ? QualityOutputMode::Upscale2x
                                          : QualityOutputMode::Upscale4x;
  }
  switch (mode) {
    case QualityOutputMode::Restore:
      dstWidth = srcW;
      dstHeight = srcH;
      break;
    case QualityOutputMode::Upscale2x:
      dstWidth = srcW * 2;
      dstHeight = srcH * 2;
      break;
    case QualityOutputMode::Upscale4x:
      dstWidth = srcW * scale;
      dstHeight = srcH * scale;
      break;
    default:
      dstWidth = srcW * 2;
      dstHeight = srcH * 2;
      break;
  }
  // 推理分辨率: x4 输出 = 目标分辨率 → 推理 = 目标 / scale
  // 如 Upscale2x: 1280×720 → 输出 2560×1440 → 推理 640×360 → x4 = 2560×1440
  inferWidth = dstWidth / scale;
  inferHeight = dstHeight / scale;
}

void VkQEnhanceLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba8;
  if (!glslPath.empty()) {
    shader->loadShaderModule(glslPath);
  }
  std::vector<UBOLayoutItem> items;
  items.push_back(
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
  items.push_back(
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
  if (constBuf) {
    items.push_back(
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
  }
  layout->addSetLayout(items);
  layout->generateLayout();
}

void VkQEnhanceLayer::onInitLayer() {
  calcOutputSize();
  outFormats[0].width = dstWidth;
  outFormats[0].height = dstHeight;
}

bool VkQEnhanceLayer::loadModel() {
  if (modelLoaded) return true;
  std::string modelPath = getModelFilePath("quality/realesrgan-general-x4v3.onnx");
  // ① OpenVINO (avox_openvino plugin 装了 + GPU/CPU 可用)
  //    plugin 没装时 openvinoEngineHub.create 返回 nullptr, 自然降级 ORT
  ovEngine.reset(AvoxManager::Get().openvinoEngineHub.create("openvino"));
  if (ovEngine && ovEngine->loadModel(modelPath, inferHeight, inferWidth, scale)) {
    useOpenVino = true;
    modelLoaded = true;
    log(LogLevel::info, "VkQEnhanceLayer: OpenVINO ", ovEngine->device(),
        " infer=", inferWidth, "x", inferHeight);
    return true;
  }
  ovEngine.reset();
  // ② ORT CPU 兜底 (现状路径; 8线程: SRVGGNetCompact 串行网络调度开销小)
  OnnxModelUser user;
  onnxSession = user.session(OnnxModel::RealESRGanX4V3, false, 0, 8);
  if (!onnxSession) {
    log(LogLevel::error, "VkQEnhanceLayer: load model failed (OpenVINO + ORT)");
    return false;
  }
  auto inputNames = onnxSession->getInputNames();
  auto outputNames = onnxSession->getOutputNames();
  if (inputNames.empty() || outputNames.empty()) {
    log(LogLevel::error, "VkQEnhanceLayer: model has no input/output names");
    return false;
  }
  inputName = inputNames[0];
  outputName = outputNames[0];
  useOpenVino = false;
  modelLoaded = true;
  log(LogLevel::info, "VkQEnhanceLayer: ORT CPU (fallback) input=", inputName);
  return true;
}

void VkQEnhanceLayer::onInitVkBuffer() {
  // 预处理 buffer: 推理分辨率 NCHW [1,3,inferH,inferW]
  int32_t preprocessSize = inferWidth * inferHeight * 3 * sizeof(float);
  preprocessBuffer = std::make_unique<VkWrapBuffer>();
  preprocessBuffer->setVkContext(vkPipeGraph);
  preprocessBuffer->initResoure(BufferUsage::store, preprocessSize,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  // 后处理 buffer: x4 推理输出 NCHW [1,3,inferH*4,inferW*4]
  int32_t postSize = (inferWidth * scale) * (inferHeight * scale) * 3 * sizeof(float);
  postprocessBuffer = std::make_unique<VkWrapBuffer>();
  postprocessBuffer->setVkContext(vkPipeGraph);
  postprocessBuffer->initResoure(BufferUsage::store, postSize,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  // 后处理独立 command
  postprocessCmd = std::make_unique<VkCommand>();
  postprocessCmd->setVkContext(vkPipeGraph);
  // 加载模型
  loadModel();
  // preprocess dispatch 尺寸: 用推理分辨率 (不是输入分辨率)
  sizeX = divUp(inferWidth, groupX);
  sizeY = divUp(inferHeight, groupY);
  // UBO: 推理分辨率的 width, height
  int32_t ubo[] = {inferWidth, inferHeight};
  updateUBO(ubo);
  log(LogLevel::info, "VkQEnhanceLayer: infer=", inferWidth, "x", inferHeight,
      " dst=", dstWidth, "x", dstHeight,
      " x4out=", inferWidth * scale, "x", inferHeight * scale);
  // 初始化 postprocess shader pipeline
  initPostprocessPipe();
  // 启动推理线程
  if (modelLoaded) {
    startTask();
  }
}

void VkQEnhanceLayer::initPostprocessPipe() {
  std::string shaderPath = getAvoxPath() + "/assets/glsl/quality_postprocess.comp.spv";
  VkShaderModule postprocessModule = loadShader(shaderPath.c_str(), vkDevice);
  if (!postprocessModule) {
    log(LogLevel::error, "VkQEnhanceLayer: postprocess shader load failed");
    return;
  }
  postprocessStageInfo = {};
  postprocessStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  postprocessStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  postprocessStageInfo.module = postprocessModule;
  postprocessStageInfo.pName = "main";
  // layout: binding 0=SSBO(input NCHW), binding 1=storageImage(out RGBA)
  postprocessLayout = std::make_unique<UBOLayout>();
  postprocessLayout->setVkContext(vkPipeGraph);
  std::vector<UBOLayoutItem> items;
  items.push_back(
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
  items.push_back(
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT});
  postprocessLayout->addSetLayout(items);
  postprocessLayout->generateLayout(sizeof(PostprocessPushConstant));
  std::vector<void*> bufferInfos;
  bufferInfos.push_back(&postprocessBuffer->descInfo);
  outTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  bufferInfos.push_back(&outTexs[0]->descInfo);
  postprocessLayout->updateSetLayout(0, 0, bufferInfos);
  VkComputePipelineCreateInfo pipeInfo = {};
  pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeInfo.layout = postprocessLayout->pipelineLayout;
  pipeInfo.stage = postprocessStageInfo;
  vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                           &pipeInfo, nullptr, &postprocessPipeline);
}

void VkQEnhanceLayer::onInitPipe() {
  std::vector<void*> bufferInfos;
  inTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkSampler sampler = getSampled(0) ? vkPipeGraph->linearSampler : VK_NULL_HANDLE;
  inTexs[0]->descInfo.sampler = sampler;
  bufferInfos.push_back(&inTexs[0]->descInfo);
  bufferInfos.push_back(&preprocessBuffer->descInfo);
  if (constBuf) {
    bufferInfos.push_back(&constBuf->descInfo);
  }
  layout->updateSetLayout(0, 0, bufferInfos);
  auto computePipelineInfo =
      createComputePipelineInfo(layout->pipelineLayout, shader->shaderStage);
  vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                           &computePipelineInfo, nullptr, &computerPipeline);
}

void VkQEnhanceLayer::onCommand() {
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
  preprocessBuffer->addBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computerPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          layout->pipelineLayout, 0, 1,
                          layout->descSets[0].data(), 0, 0);
  // dispatch 推理分辨率 (不是输入分辨率), GPU 双线性采样自动 resize
  vkCmdDispatch(cmd, sizeX, sizeY, 1);
}

void VkQEnhanceLayer::onUpdateParamet() {
  if (paramet == oldParamet) return;
  calcOutputSize();
  outFormats[0].width = dstWidth;
  outFormats[0].height = dstHeight;
}

// ── RunTask 推理线程 ──

void VkQEnhanceLayer::onRunTask() {
  log(LogLevel::info, "quality infer: start");
  while (running()) {
    std::vector<float> input;
    {
      std::unique_lock<std::mutex> lock(inferMutex);
      // wait_for 带超时: RunTask::stopTask 只 set runflag=false + join, 不 notify 子类 inferCv
      // (基类不认识子类 cv)。无限 wait 会在取消时永远阻塞 → stopTask join 死锁(取消后再开卡死)。
      // 100ms 超时定期重检 running(), stopTask 后最多 100ms 退出; submitInfer 的 notify 仍立即响应。
      inferCv.wait_for(lock, std::chrono::milliseconds(100),
                       [&] { return inferInputReady || !running(); });
      if (!running()) break;
      input = std::move(inferInput);
      inferInputReady = false;
    }
    // 推理 (OpenVINO 或 ORT), 输出统一到 outTensor 再 memcpy postprocessBuffer
    std::vector<float> outTensor(3 * (inferWidth * scale) * (inferHeight * scale));
    auto t0 = std::chrono::steady_clock::now();
    bool ok = false;
    if (useOpenVino) {
      ok = ovEngine->infer(input.data(), outTensor.data());
    } else {
      std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> inputs = {
          {inputName, input.data(), {1, 3, inferHeight, inferWidth}}};
      std::vector<std::string> outNames = {outputName};
      std::vector<std::vector<float>> outputs;
      ok = onnxSession->runShaped(inputs, outNames, outputs);
      if (ok && !outputs.empty()) outTensor.swap(outputs[0]);
    }
    auto t1 = std::chrono::steady_clock::now();
    auto inferMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    if (!ok) {
      log(LogLevel::error, "quality infer: failed");
      continue;
    }
    log(LogLevel::info, "quality infer:", inferMs, "ms");
    // 写入 postprocessBuffer (CPU→GPU SSBO)
    float* dst = reinterpret_cast<float*>(postprocessBuffer->getCpuData());
    memcpy(dst, outTensor.data(), outTensor.size() * sizeof(float));
    postprocessBuffer->flush(false);
    // 通知主线程
    {
      std::lock_guard<std::mutex> lock(inferMutex);
      inferOutputReady = true;
    }
  }
  log(LogLevel::info, "quality infer: exit");
}

void VkQEnhanceLayer::submitInfer(std::vector<float>&& input) {
  std::lock_guard<std::mutex> lock(inferMutex);
  inferInput = std::move(input);
  inferInputReady = true;
  inferCv.notify_one();
}

bool VkQEnhanceLayer::pollInferResult() {
  std::lock_guard<std::mutex> lock(inferMutex);
  if (!inferOutputReady) return false;
  inferOutputReady = false;
  hasPostprocessResult = true;
  return true;
}

void VkQEnhanceLayer::runPostprocess() {
  int32_t w4 = inferWidth * scale;
  int32_t h4 = inferHeight * scale;
  VkCommandBuffer cmd = postprocessCmd->getCommandBuffer();
  postprocessBuffer->addBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_SHADER_READ_BIT);
  outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT);
  PostprocessPushConstant pc = {w4, h4, dstWidth, dstHeight};
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, postprocessPipeline);
  vkCmdPushConstants(cmd, postprocessLayout->pipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          postprocessLayout->pipelineLayout, 0, 1,
                          postprocessLayout->descSets[0].data(), 0, 0);
  vkCmdDispatch(cmd, divUp(dstWidth, 16), divUp(dstHeight, 16), 1);
  outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_READ_BIT);
  postprocessCmd->submit();
}

// ── 主线程帧处理 ──

bool VkQEnhanceLayer::onFrame() {
  if (!modelLoaded) return true;
  // 检查推理线程是否有新结果
  bool hasNew = pollInferResult();
  if (hasNew) {
    runPostprocess();
  }
  // 抽帧: 不是每帧都提交推理
  frameCount++;
  bool shouldInfer = (paramet.skipFrames == 0) ||
                     (frameCount % (paramet.skipFrames + 1) == 1);
  if (!shouldInfer) return true;
  // map preprocessBuffer (GPU shader 已写入 NCHW float32, 推理分辨率)
  preprocessBuffer->flush(true);
  float* inputData = reinterpret_cast<float*>(preprocessBuffer->getCpuData());
  std::vector<float> cpuInput(inputData, inputData + 3 * inferWidth * inferHeight);
  submitInfer(std::move(cpuInput));
  return true;
}

}
