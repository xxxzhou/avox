#pragma once

#include "InpaintTypes.hpp"
#include <functional>
#include "avox/AvoxVision.h"

namespace avox {

// ============== 水印去除配置 ==============

struct InpaintConfig {
  // === 模型配置 ===
  ModelLevel modelLevel = ModelLevel::base;
  InpaintMode modelType = InpaintMode::lama;  // 默认使用 LaMa 模型
  DetectMode detectMode = DetectMode::yolo;  // 默认使用 YOLO 检测

  // === 检测参数 ===
  float detectThreshold = 0.2f;      // 检测置信度阈值
  float nmsThreshold = 0.4f;         // NMS IoU 阈值

  // === Mask 参数 (形态学操作) ===
  int maskDilate = 20;               // Mask 膨胀像素 (全覆盖优先)
  int maskErode = 0;                 // Mask 腐蚀像素 (已禁用)
  int maskBlur = 5;                  // Mask 边缘模糊 (高斯平滑)
  bool maskClose = false;            // 闭运算 (已禁用，避免腐蚀)
  bool maskOpen = false;             // 开运算 (已禁用，避免腐蚀)

  // === 类别特定检测参数 ===
  float textThreshold = 0.35f;       // text 类别的置信度阈值 (可更低)
  float transparentThreshold = 0.3f; // transparent 类别的置信度阈值 (需要更低)
  int minWatermarkSize = 10;         // 最小水印尺寸 (像素)

  // === 修复参数 ===
  bool useTiled = false;             // 分块修复 (大图像)
  int tileSize = 512;                // 分块大小
  int tileOverlap = 32;              // 分块重叠

  // === 性能参数 ===
  bool useGPU = true;                // 使用 GPU 加速
  int gpuDeviceId = 0;               // GPU 设备 ID
  int numThreads = 16;               // CPU 线程数 (提升到 16)

  // === 视频处理 ===
  bool useTemporalSmooth = false;    // 时序平滑 (减少闪烁)
  int smoothFrames = 5;              // 平滑窗口大小
};

}
