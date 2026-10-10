#pragma once

#include <memory>

#include "../AvoxLayer.h"
#include "../video/VideoFrame.hpp"
#include "../video/WindowRender.hpp"

namespace avox {

// 编码前视频滤镜槽位: 渲染输出之后、帧队列/封装之前的离线处理级
// (TranscodeRecorder 只认本接口, 具体滤镜为外部实现, 经 create* 工厂注入)
// 线程契约: prepare=源线程(onReady, 一次) / capture=解码回调线程(每帧,
// render后) / process=编码线程(每帧); 三处不同线程, 实现自管同步
class IRecordVideoFilter {
 public:
  virtual ~IRecordVideoFilter() = default;

 public:
  virtual const char* name() const = 0;
  // 与录制模式相容判定(open期调): 空输出(离屏直出)与视频直拷下多数滤镜无意义
  virtual bool accepts(bool bNoOutput, bool bVideoCopy) const = 0;
  // 自装配渲染接线(enableImage等); 返回true时outDesc=最终输出desc
  // 契约: 输出像素类型须匹配编码模式(硬编nv12/软编yuv420P), 错配FFVEncoder
  // 按(codecCtx->pix_fmt)读平面会越界崩溃
  // (render入参为WindowRender: 接线走ISurfaceRender面, 假帧渲染走IImageRender面)
  virtual bool prepare(const VideoDesc& srcDesc, bool bHardEncode,
                       WindowRender* render, VideoDesc& outDesc) = 0;
  // render后取帧(深拷归滤镜), false=跳帧
  virtual bool capture(int64_t pts, int64_t dts, VideoFramePtr& out) = 0;
  // 编码线程: 队列帧→编码器YUVFrame, false=丢帧
  virtual bool process(const VideoFramePtr& in, YUVFrame& out) = 0;
};

// 离线画质增强滤镜工厂(定义在QEnhanceFilter.cpp; 录制器经此注入不感知具体类型)
std::shared_ptr<IRecordVideoFilter> createQualityEnhanceFilter(
    const QualityEnhanceParamet& paramet);

}
