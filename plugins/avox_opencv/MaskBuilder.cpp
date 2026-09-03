#include "MaskBuilder.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

namespace avox {

MaskBuilder::MaskBuilder() = default;
MaskBuilder::~MaskBuilder() = default;

// ============ 辅助 ============

cv::Mat MaskBuilder::toGray(IImageBuffer* buf) {
  // 任意格式 → BGR → 单通道 gray (掩码位运算统一在单通道上做)
  cv::Mat bgr = avox::imageBufferToBgr(buf);
  if (bgr.empty()) return cv::Mat();
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  return gray;
}

IImageBuffer* MaskBuilder::matToBuffer(const cv::Mat& mat) {
  // CV_8UC1 → r8 IImageBuffer (新建, 调用方释放)
  if (mat.empty()) return nullptr;
  IImageBuffer* buf = createImageBuffer();
  if (!buf) return nullptr;
  if (!avox::mat2ImageBuffer(mat, buf)) {
    delete buf;
    return nullptr;
  }
  return buf;
}

// ============ 几何掩码 ============

IImageBuffer* MaskBuilder::buildSectorMask(int32_t w, int32_t h, int32_t cx, int32_t cy,
                                           int32_t rx, int32_t ry, double startAngle,
                                           double endAngle) {
  if (w <= 0 || h <= 0) {
    lastError = "buildSectorMask: invalid size";
    return nullptr;
  }
  cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);
  // OpenCV 椭圆: 圆心 (cx,cy), 半轴 (rx,ry), 旋转 0, [startAngle,endAngle] 度 (0=右/东 顺时针), 实心白
  cv::ellipse(mask, cv::Point(cx, cy), cv::Size(rx, ry), 0, startAngle, endAngle,
              cv::Scalar(255), -1);
  return matToBuffer(mask);
}

IImageBuffer* MaskBuilder::buildCircleMask(int32_t w, int32_t h, int32_t cx, int32_t cy,
                                           int32_t radius) {
  if (w <= 0 || h <= 0) {
    lastError = "buildCircleMask: invalid size";
    return nullptr;
  }
  cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);
  cv::circle(mask, cv::Point(cx, cy), radius, cv::Scalar(255), -1);
  return matToBuffer(mask);
}

// ============ 颜色范围掩码 ============

IImageBuffer* MaskBuilder::buildColorRangeMask(IImageBuffer* src, ColorSpace cs, int32_t lo0,
                                               int32_t lo1, int32_t lo2, int32_t hi0, int32_t hi1,
                                               int32_t hi2) {
  if (!src) {
    lastError = "buildColorRangeMask: null src";
    return nullptr;
  }
  cv::Mat bgr = avox::imageBufferToBgr(src);
  if (bgr.empty()) {
    lastError = "buildColorRangeMask: unsupported format";
    return nullptr;
  }
  // imageBufferToBgr 已归一到 BGR; 按 cs 转换到目标色彩空间
  cv::Mat work;
  switch (cs) {
    case ColorSpace::rgb:
      cv::cvtColor(bgr, work, cv::COLOR_BGR2RGB);
      break;
    case ColorSpace::hsv:
      cv::cvtColor(bgr, work, cv::COLOR_BGR2HSV);  // H 范围 0-179 (OpenCV 约定)
      break;
    case ColorSpace::bgr:
    default:
      work = bgr;
      break;
  }
  cv::Mat mask;
  cv::inRange(work, cv::Scalar(lo0, lo1, lo2), cv::Scalar(hi0, hi1, hi2), mask);
  return matToBuffer(mask);
}

// ============ 位运算组合 ============

IImageBuffer* MaskBuilder::maskAnd(IImageBuffer* a, IImageBuffer* b) {
  cv::Mat ga = toGray(a);
  cv::Mat gb = toGray(b);
  if (ga.empty() || gb.empty()) {
    lastError = "maskAnd: null/invalid input";
    return nullptr;
  }
  if (ga.size() != gb.size()) {
    lastError = "maskAnd: size mismatch";
    return nullptr;
  }
  cv::Mat out;
  cv::bitwise_and(ga, gb, out);
  return matToBuffer(out);
}

IImageBuffer* MaskBuilder::maskOr(IImageBuffer* a, IImageBuffer* b) {
  cv::Mat ga = toGray(a);
  cv::Mat gb = toGray(b);
  if (ga.empty() || gb.empty()) {
    lastError = "maskOr: null/invalid input";
    return nullptr;
  }
  if (ga.size() != gb.size()) {
    lastError = "maskOr: size mismatch";
    return nullptr;
  }
  cv::Mat out;
  cv::bitwise_or(ga, gb, out);
  return matToBuffer(out);
}

IImageBuffer* MaskBuilder::maskNot(IImageBuffer* a) {
  cv::Mat ga = toGray(a);
  if (ga.empty()) {
    lastError = "maskNot: null/invalid input";
    return nullptr;
  }
  cv::Mat out;
  cv::bitwise_not(ga, out);
  return matToBuffer(out);
}

const char* MaskBuilder::getLastError() { return lastError.c_str(); }

}
