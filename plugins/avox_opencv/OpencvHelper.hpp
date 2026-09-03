#pragma once

// OpenCV 图像转换工具

#include "avox/AvoxVideo.h"
#include <opencv2/opencv.hpp>

namespace avox {

// ============ cv 异常装甲宏（2026-08-15 实机 P0）============
// 坏帧/边界尺寸会让 cv:: 抛 cv::Exception，异常穿过绑定层 std::terminate 宿主
// Python 进程（0xc0000409 fail-fast，无法 catch）。所有吃 IImageBuffer 的插件
// 公共入口必须用此宏包住函数体：异常 → lastError 归因 + 返回失败值。
// 用法：
//   int32_t C::fn(...) {
//     AVOX_CV_TRY;
//     ...原函数体...
//     AVOX_CV_CATCH_RET(0);   // 参数 = 失败返回值（数值 0 / false / nullptr）
//   }
#define AVOX_CV_TRY try {
#define AVOX_CV_CATCH_RET(ret)                                                     \
  }                                                                               \
  catch (const cv::Exception &e_) {                                               \
    lastError = std::string("cv exception: ") + e_.what();                        \
    return ret;                                                                   \
  }                                                                               \
  catch (const std::exception &e_) {                                              \
    lastError = std::string("exception: ") + e_.what();                           \
    return ret;                                                                   \
  }                                                                               \
  catch (...) {                                                                   \
    lastError = "unknown exception";                                              \
    return ret;                                                                   \
  }

// 获取 OpenCV Mat 类型
int32_t getMatType(const ImageType &format);

// OpenCV Mat 类型转 ImageType
ImageType getMat2ImageType(int32_t matType);

// ============ 零拷贝版本 (单线程流水处理用) ============

// IImageBuffer -> cv::Mat (零拷贝，支持 rowPitch)
// 返回的 Mat 直接引用 buffer 数据，buffer 必须在使用期间保持有效
cv::Mat imageBuffer2MatRef(IImageBuffer *buffer);

// cv::Mat -> IImageBuffer (直接写入已分配的 buffer)
bool matRef2ImageBuffer(const cv::Mat &mat, IImageBuffer *buffer);

// ============ 深拷贝版本 ============

// IImageBuffer -> cv::Mat (深拷贝)
cv::Mat imageBuffer2Mat(IImageBuffer *buffer);

// cv::Mat -> IImageBuffer (深拷贝)
bool mat2ImageBuffer(const cv::Mat &mat, IImageBuffer *buffer);

// IImageBuffer -> BGR cv::Mat (CV_8UC3 深拷贝)
// 归一 rgba8/bgra8/rgb8/bgr8/r8 等通道顺序为 BGR; 失败返回空 Mat
cv::Mat imageBufferToBgr(IImageBuffer *buffer);

}
