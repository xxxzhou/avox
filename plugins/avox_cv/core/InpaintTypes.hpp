#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "avox/AvoxVision.h"
#include "avox/AvoxVideo.h"

namespace avox {

// ============== 水印去除相关类型 ==============

// 检测模式
enum class DetectMode {
  auto_detect,      // 完全自动检测 (帧差分 + 边缘分析，用于视频)
  yolo,             // YOLO 检测 (需要训练模型)
  hybrid,           // 混合检测 (自动 + YOLO)
  static_image      // 静态图片检测 (边缘/颜色/角落分析，单图专用)
};

// 水印类型
enum class WatermarkType {
  text = 0,       // 文字水印
  logo = 1,       // Logo 水印
  icon = 2,       // 图标水印
  timestamp = 3,  // 时间戳
  unknown = 99    // 未知类型
};

// 检测框
struct WatermarkBBox {
  float x = 0;           // 左上角 x
  float y = 0;           // 左上角 y
  float width = 0;       // 宽度
  float height = 0;      // 高度
  float confidence = 0;  // 置信度
  int classId = 0;       // 类别 ID
};

// 分割结果 (YOLO-Seg 输出)
struct WatermarkSeg {
  WatermarkBBox bbox;                 // 边界框
  std::vector<uint8_t> mask;          // 精确分割掩码 (0=背景, 255=水印)
  int maskWidth = 0;                  // 掩码宽度
  int maskHeight = 0;                 // 掩码高度

  bool valid() const { return !mask.empty() && maskWidth > 0 && maskHeight > 0; }
  size_t maskSize() const { return maskWidth * maskHeight; }
};

// 水印信息
struct WatermarkInfo {
  WatermarkBBox bbox;
  WatermarkType type = WatermarkType::unknown;
};

// 注意: 图像数据使用 IImageBuffer 接口 (定义在 avox/AvoxVideo.h)
// 所有 inpaint 模块统一使用 IImageBuffer* 作为图像参数

// Mask 数据 (单通道, 255=修复, 0=保持)
struct InpaintMask {
  const uint8_t* data = nullptr;
  int width = 0;
  int height = 0;

  bool valid() const { return data && width > 0 && height > 0; }
  size_t size() const { return width * height; }
};

// 处理结果
struct InpaintResult {
  bool success = false;
  std::vector<WatermarkInfo> watermarks;
  float detectTimeMs = 0;
  float inpaintTimeMs = 0;
  float totalTimeMs = 0;
};

}
