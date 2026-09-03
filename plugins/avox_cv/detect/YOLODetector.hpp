#pragma once

#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModelUser.hpp"
#include "avox/module/AvoxManager.hpp"
#include "../core/InpaintTypes.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace avox {

// ============== YOLO 水印检测器 ==============

class YOLODetector : public OnnxModelUser {
 public:
  YOLODetector();
  ~YOLODetector();

  // 打开 (经全局缓存取 session; 按 ModelLevel 由调用方选 OnnxModel)
  bool open(OnnxModel model, bool useGPU = true);

  // 关闭 (清自身指针与缓冲, 不释放共享模型)
  void close();

  // 是否就绪
  bool ready() const { return session && session->isLoaded(); }

  // 检测水印 (返回边界框)
  std::vector<WatermarkBBox> detect(const uint8_t* rgbImage,
                                     int width, int height,
                                     float confThreshold = 0.5f,
                                     float nmsThreshold = 0.4f);

  // 检测水印 (返回分割掩码) - YOLO-Seg
  std::vector<WatermarkSeg> detectWithMask(const uint8_t* rgbImage,
                                            int width, int height,
                                            float confThreshold = 0.5f,
                                            float nmsThreshold = 0.4f);

  // 获取输入尺寸
  int getInputSize() const { return inputSize; }

  // 是否为分割模型
  bool isSegmentationModel() const { return isSegModel; }

 private:
  IONNXSession* session = nullptr;  // 借用自 OnnxSessionCache(Shared), 对象不释放
  int inputSize = 640;
  int numClasses = 4;  // 水印类别数
  bool isSegModel = false;  // 是否为分割模型
  int maskSize = 160;  // 分割掩码尺寸

  // 动态获取的输入名称 (适配不同 YOLO 模型)
  std::string imageInputName;

  // 预处理缓冲
  std::vector<float> preprocessed;

  // 预处理: 直接 resize + 归一化 (和训练时一致)
  void letterbox(const uint8_t* image, int width, int height,
                 float& scaleX, float& scaleY);

  // 后处理: 解析 YOLO 检测输出
  std::vector<WatermarkBBox> postprocess(const float* outputData,
                                          int numAnchors, int channels,
                                          int origWidth, int origHeight,
                                          float scaleX, float scaleY,
                                          float confThresh, float nmsThresh);

  // 后处理: 解析 YOLO-Seg 分割输出
  std::vector<WatermarkSeg> postprocessSeg(const float* detOutput,
                                            const float* protoOutput,
                                            int numAnchors, int channels,
                                            int origWidth, int origHeight,
                                            float scaleX, float scaleY,
                                            float confThresh, float nmsThresh);

  // NMS 非极大值抑制
  std::vector<int> nms(const std::vector<WatermarkBBox>& boxes, float nmsThresh);

  // 计算 IoU
  static float computeIoU(const WatermarkBBox& a, const WatermarkBBox& b);

  // Sigmoid 函数
  static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }
};

}
