#include "MaskProcessor.hpp"

// TODO: OpenCV 版本待修复链接问题后启用
// #ifdef AVOX_ENABLE_OPENCV
// #include <opencv2/opencv.hpp>
// #endif

namespace avox {

void MaskProcessor::process(std::vector<uint8_t>& mask, int width, int height,
                            int dilateSize, int erodeSize, int blur,
                            bool useClose, bool useOpen) {
  // 使用 CPU 版本（OpenCV 版本链接有问题，暂时禁用）
  processCPU(mask, width, height, dilateSize, erodeSize, blur, useClose, useOpen);
}

// #ifdef AVOX_ENABLE_OPENCV
// void MaskProcessor::processOpenCV(...) { ... }
// #endif

void MaskProcessor::processCPU(std::vector<uint8_t>& mask, int width, int height,
                                int dilateSize, int erodeSize, int blur,
                                bool useClose, bool useOpen) {
  // 开运算 (先腐蚀后膨胀)
  if (useOpen && erodeSize > 0) {
    erode(mask, width, height, erodeSize);
    dilate(mask, width, height, erodeSize);
  }

  // 闭运算 (先膨胀后腐蚀)
  if (useClose && dilateSize > 0) {
    dilate(mask, width, height, dilateSize);
    erode(mask, width, height, dilateSize);
  } else if (dilateSize > 0) {
    dilate(mask, width, height, dilateSize);
  }

  // 单独腐蚀
  if (erodeSize > 0 && !useOpen) {
    erode(mask, width, height, erodeSize);
  }

  // 高斯模糊
  if (blur > 0) {
    gaussianBlur(mask, width, height, blur);
  }
}

void MaskProcessor::gaussianBlur(std::vector<uint8_t>& mask, int width, int height, int kernelSize) {
  if (kernelSize <= 0) return;

  // 确保 kernel size 为奇数
  kernelSize = kernelSize * 2 + 1;
  if (kernelSize > width || kernelSize > height) {
    kernelSize = std::min(width, height);
    if (kernelSize % 2 == 0) kernelSize--;
    if (kernelSize < 3) return;
  }

  std::vector<uint8_t> temp(mask.size());

  // 简化的高斯模糊 (使用均值模糊近似)
  int radius = kernelSize / 2;

  // 水平方向模糊
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int sum = 0;
      int count = 0;
      for (int k = -radius; k <= radius; k++) {
        int nx = x + k;
        if (nx >= 0 && nx < width) {
          sum += mask[y * width + nx];
          count++;
        }
      }
      temp[y * width + x] = static_cast<uint8_t>(sum / count);
    }
  }

  // 垂直方向模糊
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int sum = 0;
      int count = 0;
      for (int k = -radius; k <= radius; k++) {
        int ny = y + k;
        if (ny >= 0 && ny < height) {
          sum += temp[ny * width + x];
          count++;
        }
      }
      mask[y * width + x] = static_cast<uint8_t>(sum / count);
    }
  }
}

void MaskProcessor::dilate(std::vector<uint8_t>& mask, int width, int height, int radius) {
  if (radius <= 0) return;

  std::vector<uint8_t> result(mask.size(), 0);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // 检查膨胀区域
      for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            if (mask[ny * width + nx] > 128) {
              result[y * width + x] = 255;
              break;
            }
          }
        }
        if (result[y * width + x] == 255) break;
      }
    }
  }

  mask = std::move(result);
}

void MaskProcessor::erode(std::vector<uint8_t>& mask, int width, int height, int radius) {
  if (radius <= 0) return;

  std::vector<uint8_t> result(mask.size(), 0);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // 检查腐蚀区域 - 只有全部为前景才保留
      bool allForeground = true;
      for (int dy = -radius; dy <= radius && allForeground; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            if (mask[ny * width + nx] < 128) {
              allForeground = false;
              break;
            }
          }
        }
      }
      result[y * width + x] = allForeground ? 255 : 0;
    }
  }

  mask = std::move(result);
}

}