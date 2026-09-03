#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>

namespace avox {

// IMaskBuilder 的 OpenCV 实现: 通用二值掩码生成 (几何形状 + 颜色 inRange + 位运算)。
// 由 OpencvModule 注册为工厂 "opencv", 业务经 maskBuilderHub.create("opencv") 取实例。
// 不含场景知识: 所有角度/颜色/半径由调用方传入, 场景特化的掩码组合在上层完成。
class MaskBuilder : public IMaskBuilder {
 private:
  // IImageBuffer → 单通道 gray cv::Mat (位运算用; 任意格式归一)
  static cv::Mat toGray(IImageBuffer* buf);
  // cv::Mat (CV_8UC1) → 新建 IImageBuffer (r8, 调用方释放); 失败 nullptr
  static IImageBuffer* matToBuffer(const cv::Mat& mat);
  std::string lastError;

 public:
  MaskBuilder();
  ~MaskBuilder() override;

 public:
  IImageBuffer* buildSectorMask(int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t rx,
                                int32_t ry, double startAngle, double endAngle) override;
  IImageBuffer* buildCircleMask(int32_t w, int32_t h, int32_t cx, int32_t cy,
                                int32_t radius) override;
  IImageBuffer* buildColorRangeMask(IImageBuffer* src, ColorSpace cs, int32_t lo0, int32_t lo1,
                                    int32_t lo2, int32_t hi0, int32_t hi1, int32_t hi2) override;
  IImageBuffer* maskAnd(IImageBuffer* a, IImageBuffer* b) override;
  IImageBuffer* maskOr(IImageBuffer* a, IImageBuffer* b) override;
  IImageBuffer* maskNot(IImageBuffer* a) override;
  const char* getLastError() override;
};

}
