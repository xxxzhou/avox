#pragma once

#include "InpaintTypes.hpp"
#include <vector>
#include <cstdint>

namespace avox {

// ============== Mask 形态学处理器 ==============
// 使用 OpenCV 进行形态学操作，提升 mask 质量

class MaskProcessor {
 public:
  MaskProcessor() = default;
  ~MaskProcessor() = default;

  // 处理 mask (形态学操作 + 模糊)
  // 输入/输出均为单通道灰度图 (width * height)
  void process(std::vector<uint8_t>& mask, int width, int height,
               int dilateSize, int erodeSize, int blur,
               bool useClose, bool useOpen);

 private:
#ifdef AVOX_ENABLE_OPENCV
  // OpenCV 版本
  void processOpenCV(std::vector<uint8_t>& mask, int width, int height,
                     int dilateSize, int erodeSize, int blur,
                     bool useClose, bool useOpen);
#endif

  // 纯 CPU 版本 (无 OpenCV 时的备选)
  void processCPU(std::vector<uint8_t>& mask, int width, int height,
                  int dilateSize, int erodeSize, int blur,
                  bool useClose, bool useOpen);

  // 手动高斯模糊
  void gaussianBlur(std::vector<uint8_t>& mask, int width, int height, int kernelSize);

  // 手动膨胀
  void dilate(std::vector<uint8_t>& mask, int width, int height, int radius);

  // 手动腐蚀
  void erode(std::vector<uint8_t>& mask, int width, int height, int radius);
};

}