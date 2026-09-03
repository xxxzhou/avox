#include "OpencvHelper.hpp"
#include <cstring>

namespace avox {

int32_t getMatType(const ImageType &format) {
  switch (format) {
    case ImageType::rgba8:
    case ImageType::bgra8:
      return CV_8UC4;
    case ImageType::rgb8:
    case ImageType::bgr8:
      return CV_8UC3;
    case ImageType::r16:
      return CV_16UC1;
    case ImageType::r32f:
      return CV_32FC1;
    case ImageType::r32:
      return CV_32SC1;
    case ImageType::rgba32f:
      return CV_32FC4;
    case ImageType::rgba32:
      return CV_32SC4;
    case ImageType::r8:
      return CV_8UC1;
    default:
      return CV_8UC4;
  }
}

ImageType getMat2ImageType(int32_t matType) {
  switch (matType) {
    case CV_8UC4:
      return ImageType::rgba8;
    case CV_8UC1:
      return ImageType::r8;
    case CV_8UC3:
      return ImageType::rgb8;
    case CV_16UC1:
      return ImageType::r16;
    case CV_32FC1:
      return ImageType::r32f;
    case CV_32SC1:
      return ImageType::r32;
    case CV_32FC4:
      return ImageType::rgba32f;
    default:
      return ImageType::rgba8;
  }
}

cv::Mat imageBuffer2MatRef(IImageBuffer *buffer) {
  if (!buffer) return cv::Mat();

  ImageFormat format = buffer->getImageFormat();
  int32_t matType = getMatType(format.imageType);
  int32_t pixelBytes = CV_ELEM_SIZE(matType);
  int32_t rowPitch = format.rowPitch > 0 ? format.rowPitch : format.width * pixelBytes;

  // 直接引用 buffer 数据，step = rowPitch
  return cv::Mat(format.height, format.width, matType,
                 const_cast<uint8_t*>(buffer->getPointer()), rowPitch);
}

bool matRef2ImageBuffer(const cv::Mat &mat, IImageBuffer *buffer) {
  if (!buffer || mat.empty()) return false;

  uint8_t *dst = buffer->getPointer();
  if (!dst) return false;

  ImageFormat format = buffer->getImageFormat();
  int32_t srcRowPitch = mat.step;
  int32_t dstRowPitch = format.rowPitch;
  int32_t rowBytes = mat.cols * mat.elemSize();

  if (dstRowPitch == srcRowPitch) {
    std::memcpy(dst, mat.data, srcRowPitch * mat.rows);
  } else {
    for (int32_t y = 0; y < mat.rows; y++) {
      std::memcpy(dst + y * dstRowPitch, mat.ptr(y), rowBytes);
    }
  }
  return true;
}

cv::Mat imageBuffer2Mat(IImageBuffer *buffer) {
  if (!buffer) return cv::Mat();
  return imageBuffer2MatRef(buffer).clone();
}

bool mat2ImageBuffer(const cv::Mat &mat, IImageBuffer *buffer) {
  if (!buffer || mat.empty()) return false;

  ImageFormat format = {};
  format.width = mat.cols;
  format.height = mat.rows;
  format.imageType = getMat2ImageType(mat.type());
  format.rowPitch = mat.step;

  buffer->setImageFormat(format);
  return matRef2ImageBuffer(mat, buffer);
}

cv::Mat imageBufferToBgr(IImageBuffer *buffer) {
  if (!buffer) return cv::Mat();
  ImageFormat fmt = buffer->getImageFormat();
  if (!fmt.bVailid()) return cv::Mat();
  cv::Mat raw = imageBuffer2MatRef(buffer);
  if (raw.empty()) return cv::Mat();
  cv::Mat bgr;
  switch (fmt.imageType) {
    case ImageType::bgr8:
      bgr = raw;
      break;
    case ImageType::rgb8:
      cv::cvtColor(raw, bgr, cv::COLOR_RGB2BGR);
      break;
    case ImageType::bgra8:
      cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
      break;
    case ImageType::rgba8:
      cv::cvtColor(raw, bgr, cv::COLOR_RGBA2BGR);
      break;
    case ImageType::r8:
      cv::cvtColor(raw, bgr, cv::COLOR_GRAY2BGR);
      break;
    default:
      break;
  }
  if (bgr.empty()) {
    // 兜底: 按通道数尽力转
    int c = raw.channels();
    if (c == 4) {
      cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
    } else if (c == 3) {
      bgr = raw;
    } else if (c == 1) {
      cv::cvtColor(raw, bgr, cv::COLOR_GRAY2BGR);
    } else {
      return cv::Mat();
    }
  }
  return bgr.clone();
}

}
