#pragma once

#include "avox/AvoxDef.h"
#include "avox/AvoxVideo.h"  // IImageBuffer / ImageType

namespace avox {

// 图像处理插件接口 (avox_opencv 实现 cv::imread/cv::resize; 核心经 imageProcHub 调用)。
// 可扩展: 未来新增 opencv 图像处理能力就往这里加方法, 实现在 avox_opencv。
// 跨 DLL 安全: 签名只用 IImageBuffer*/原始指针/POD, 不传 STL (同 ITemplateMatcher, AvoxVision.h:43-46)。
// 内部 C++ 接口 -> 走 .hpp (项目约定 .h=公共 C 接口), 不进公共 AvoxVideo.h。
class IImageProc {
 public:
  virtual ~IImageProc() = default;
  // stb 不支持的格式(WEBP/HEIF 等)加载回退; 成功把像素写入 buffer 返回 true
  virtual bool loadPath(const char* path, IImageBuffer* buffer) = 0;
  // 8 位 packed 格式 SIMD 缩放; 返回 false 表示该 imageType 不支持(调用方回退手写双线性)
  virtual bool resize8u(const uint8_t* inData, int32_t inW, int32_t inH, int32_t inPitch,
                        uint8_t* outData, int32_t outW, int32_t outH, int32_t outPitch,
                        ImageType imageType) = 0;
};

}
