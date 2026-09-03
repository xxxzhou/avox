#pragma once

// OpenCV 图像处理实现 (IImageProc 接口): loadPath(stb 不支持的格式回退) + resize8u(SIMD)。
// OpencvModule::loadModule 时 reg "opencv" 进 imageProcHub; 核心经 hub 调用, 核心不链 opencv。

#include "avox/video/IImageProc.hpp"

namespace avox {

class ImageProc : public IImageProc {
 public:
  // stbi_load 失败回退: cv::imread(IMREAD_UNCHANGED) → 位深归一 8U → 通道序 BGR/BGRA→RGB/RGBA → 写 buffer
  bool loadPath(const char* path, IImageBuffer* buffer) override;
  // 8 位 packed 格式 SIMD 缩放(零拷贝引用 in/out); 不支持的 imageType 返回 false(调用方回退手写双线性)
  bool resize8u(const uint8_t* inData, int32_t inW, int32_t inH, int32_t inPitch,
                uint8_t* outData, int32_t outW, int32_t outH, int32_t outPitch,
                ImageType imageType) override;
};

}
