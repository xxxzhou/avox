#pragma once

#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModelUser.hpp"
#include "avox/module/AvoxManager.hpp"
#include "../core/InpaintTypes.hpp"
#include <memory>

namespace avox {

// ============== AOT-GAN 图像修复器 ==============

class AOTGanInpainter : public OnnxModelUser {
 public:
  AOTGanInpainter();
  ~AOTGanInpainter();

  // 打开 (经全局缓存取 session)
  bool open(bool useGPU = true);

  // 关闭 (清自身指针与缓冲, 不释放共享模型)
  void close();

  // 是否就绪
  bool ready() const { return session && session->isLoaded(); }

  // 修复图像
  bool inpaint(const uint8_t* rgbImage,
               const uint8_t* mask,  // 单通道, 255=修复
               int width, int height,
               uint8_t* output);

  // 设置输入尺寸
  void setInputSize(int size) { inputSize = size; }
  int getInputSize() const { return inputSize; }

  // 设置 mask 膨胀参数
  void setDilateKernel(int size, int iterations) {
    dilateKernelSize = size;
    dilateIterations = iterations;
  }

 private:
  IONNXSession* session = nullptr;  // 借用自 OnnxSessionCache(Shared), 对象不释放
  int inputSize = 512;

  // 输入输出名称 (从模型动态获取)
  std::string imageInputName;
  std::string maskInputName;
  std::string outputName;

  // Mask 膨胀参数
  int dilateKernelSize = 5;
  int dilateIterations = 3;

  // 预处理缓冲
  std::vector<float> imageTensor;
  std::vector<float> maskTensor;
  std::vector<uint8_t> dilatedMask;  // 膨胀后的 mask

  // 膨胀 mask
  void dilateMask(const uint8_t* mask, int width, int height);

  // 预处理
  void preprocess(const uint8_t* image,
                  const uint8_t* mask,
                  int width, int height);

  // 后处理
  void postprocess(const float* output,
                   const uint8_t* original,
                   const uint8_t* mask,
                   int width, int height,
                   uint8_t* result);
};

}