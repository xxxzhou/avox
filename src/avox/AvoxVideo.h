#pragma once

#include <stdint.h>

#include <cstring>

#include "AvoxDef.h"
#include "AvoxImage.h"  // IImageBuffer / ImageFormat (桥接函数用到)

namespace avox {

// name,value,group,group size,str
#define AVOX_MAP_YUV(XX)                   \
  XX(gray, 0, 1, 1, "gray")               \
  XX(yuv420P, 1, 4, 6, "yuv420P")         \
  XX(yuv422P, 2, 2, 4, "yuv422P")         \
  XX(yuv444P, 3, 1, 3, "yuv444P")         \
  XX(nv12, 4, 4, 6, "nv12")               \
  XX(yuv2I, 5, 2, 4, "yuv2I")             \
  XX(yvyuI, 6, 2, 4, "yvyuI")             \
  XX(uyvyI, 7, 2, 4, "uyvyI")             \
  XX(uyvy422_10B, 8, 2, 5, "uyvy422_10B") \
  XX(yuv420P10, 9, 4, 12, "yuv420P10")    \
  XX(yuyv422A, 10, 2, 4, "yuyv422A")

enum class YuvType : int32_t {
  other = -1,
#define XX(name, value, group, groupsize, str) name = value,
  AVOX_MAP_YUV(XX)
#undef XX
};

struct YUVFormat {
  int32_t width = 0;
  int32_t height = 0;
  YuvType type = YuvType::other;
};

struct YUVFrame {
  int64_t pts = 0;
  int64_t dts = 0;
  uint8_t* data[3] = {nullptr};
  int32_t stride[3] = {0};
  YUVFormat format = {};
  int32_t keyFrame = 0;
};

// YUV 矩阵系数规范(决定 RGB<->YUV 转换矩阵)
enum class YuvStandard { bt601, bt709, bt2020 };
// 量程: full=JPEG 0~255(屏幕/UI), limited=MPEG 16~235(广播/硬编兼容)
enum class YuvRange { full, limited };
// 颜色空间描述: shader 矩阵与 encoder tag 的共同真相源
struct ColorSpaceDesc {
  YuvStandard standard = YuvStandard::bt601;
  YuvRange range = YuvRange::full;
};

struct VideoDesc {
  int32_t width = 0;
  int32_t height = 0;
  double fps = 0;
  YuvType type = YuvType::other;
  // 颜色空间,随 VideoDesc 值拷贝流到 encoder
  ColorSpaceDesc colorSpace = {};
};

extern "C" {
AVOX_EXPORT const char* getYuvTypeStr(YuvType yuvType);
AVOX_EXPORT int32_t getYuvFrameSize(const YUVFormat& format, int32_t rowPitch);
// 是否平面格式，420P,422P,444P以及NV12(Y单独面,UV交叉)都属于平面格式
AVOX_EXPORT bool bVPlaneFormat(YuvType yuvType);

AVOX_EXPORT void yuv2ImageFormat(const YUVFrame& yuvFrame, ImageFormat& format);
AVOX_EXPORT void image2YUVFormat(const ImageFormat& imFormat, YuvType yuvType,
                                YUVFormat& format);
// 判定"单块连续 + UV步长为Y步长的整数分之一"的紧凑排布(此时连续split与packed字节重合)
// 注意它不代表无padding: yuv420P/422P带padding时packed([偶|奇|pad])与split(等距行)不一致,
// 直接引用外部帧还需满足stride[0]==width
AVOX_EXPORT bool bTightlyPacked(const YUVFrame& yuvFrame);
// 把平面frame的数据复制到紧湊的buffer中
// 确保yrowpitch是双倍,让UV交及可放一行Y,或是二行uv可放一行Y
// YUV420P/YUV422P有padding的,还会处理UV的padding适合提交GPU
AVOX_EXPORT void copyPlaneYUV2TightlyBuffer(const YUVFrame& frame,
                                           uint8_t* bfdata);
// 从IImageBuffer得到YUVFrame,Image为R8类型
AVOX_EXPORT bool image2YUVFrame(IImageBuffer* buffer, YUVFrame& yuvFrame,
                               YuvType yuvType);
// 从IImageBuffer得到split布局(逻辑行等距,stride[1]=rowPitch/2,可直接给ffmpeg逐行读)的YUVFrame
// 契约: IImageBuffer恒为packed布局(yuv420P/422P的UV物理行为[偶|奇|pad])
// 仅420P/422P且rowPitch!=width时需要重排,此时数据拷到tmp再重排(buffer不被修改),
// 布局已等价时零拷贝指向buffer; 需要重排而tmp为空时返回false
AVOX_EXPORT bool image2SplitYUVFrame(IImageBuffer* buffer, YuvType yuvType,
                                    YUVFrame& yuvFrame, IImageBuffer* tmp);

// YUVFrame → RGBA 转换（根据 format.type 自动分发，内部设置 buffer 格式）
// 注意IImageBuffer在yuv420P/yuv422P的情况下
// 如果是和GPU交互的,其UV的padding类似[前半width/2 | 后半width/2 | padding]
// 如果要给ffmpeg交互，需要改成[前半width/2 | pad/2 | 后半width/2 | pad/2]
AVOX_EXPORT bool yuvframe2Rgba(const YUVFrame& frame, IImageBuffer* buffer);
// shader把U/V按width×(height/4)紧凑打包(每物理行2条逻辑UV行),
// 加padding后每物理行布局: [前半width/2 | 后半width/2 | padding]
// 重排为: [前半width/2 | pad/2 | 后半width/2 | pad/2]
// 这样stride=rowPitch/2,FFmpeg按等距读取即可
// @deprecated 原地把packed UV重排为split, 会破坏"IImageBuffer恒packed"契约, 新代码勿用;
// 取split帧用 image2SplitYUVFrame(不修改buffer)
AVOX_EXPORT void unpackGpuYUV(IImageBuffer* buffer, YuvType yuvType);
}

}