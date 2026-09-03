#include "ImageProc.hpp"

#include "OpencvHelper.hpp"  // mat2ImageBuffer (mat→buffer, 8UC1/3/4→r8/rgb8/rgba8)
#include "avox/AvoxVideo.h"    // ImageFormat / ImageType
#include "avox/module/LogHelper.hpp"

#include <opencv2/imgcodecs.hpp>  // cv::imread
#include <opencv2/imgproc.hpp>    // cv::cvtColor / cv::resize / CV_MAKETYPE

namespace avox {

// 忠实搬移自原核心 src/avox/video/ImageIO.cpp::loadImagePathWithOpenCV(已删)。
bool ImageProc::loadPath(const char* path, IImageBuffer* buffer) {
  cv::Mat mat = cv::imread(path, cv::IMREAD_UNCHANGED);  // 保留 alpha(WEBP 常带 alpha)
  if (mat.empty()) {
    LOGFLF(LogLevel::warn, "load(opencv) ", path, " failed");
    return false;
  }
  // 位深归一到 8 位(stb 路径永远只产 8 位; 非整 8 位会破坏 rgb8=1字节/通道 的不变量)
  if (mat.depth() != CV_8U) {
    double scale = 1.0;
    if (mat.depth() == CV_16U) {
      scale = 1.0 / 257.0;  // 65535 → 255
    } else if (mat.depth() == CV_32F) {
      scale = 255.0;  // [0,1] → [0,255]
    }
    cv::Mat mat8;
    mat.convertTo(mat8, CV_8U, scale);
    mat = mat8;
  }
  // 通道顺序归一: BGR/BGRA → RGB/RGBA, 匹配 stb 路径消费者约定(都是 RGB/RGBA)
  switch (mat.channels()) {
    case 1:
      break;  // 灰度 r8, 无需转换
    case 2:
      cv::cvtColor(mat, mat, cv::COLOR_GRAY2RGBA);  // 灰度+alpha, 罕见兜底
      break;
    case 3:
      cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);  // → rgb8
      break;
    case 4:
      cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGBA);  // → rgba8
      break;
    default:
      LOGFLF(LogLevel::warn, "load(opencv) ", path,
             " unsupported channels:", mat.channels());
      return false;
  }
  // mat2ImageBuffer: setImageFormat(getMat2ImageType(mat.type()), rowPitch=mat.step) + memcpy;
  // 语义同旧核心 matToImageBuffer8U(已删): 8UC1→r8 / 8UC3→rgb8 / 8UC4→rgba8
  return mat2ImageBuffer(mat, buffer);
}

// 忠实搬移自原核心 src/avox/video/ImageIO.cpp::resizeImageWithOpenCV(已删)。
bool ImageProc::resize8u(const uint8_t* inData, int32_t inW, int32_t inH, int32_t inPitch,
                         uint8_t* outData, int32_t outW, int32_t outH, int32_t outPitch,
                         ImageType imageType) {
  int channels = 0;
  switch (imageType) {
    case ImageType::r8:
      channels = 1;
      break;
    case ImageType::rgb8:
    case ImageType::bgr8:
      channels = 3;
      break;
    case ImageType::rgba8:
    case ImageType::bgra8:
    case ImageType::argb8:
      channels = 4;
      break;
    default:
      return false;  // 16/32 位、浮点、平面(rgb8P/bgr8P)格式交给调用方手写版
  }
  // 零拷贝引用输入/输出缓冲(step=rowPitch); cv::resize 的 INTER_LINEAR 对 8UC1/3/4 有 SIMD 实现
  cv::Mat src(inH, inW, CV_MAKETYPE(CV_8U, channels), const_cast<uint8_t*>(inData), inPitch);
  cv::Mat dst(outH, outW, CV_MAKETYPE(CV_8U, channels), outData, outPitch);
  cv::resize(src, dst, dst.size(), 0, 0, cv::INTER_LINEAR);
  return true;
}

}
