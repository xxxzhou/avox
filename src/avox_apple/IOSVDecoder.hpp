#pragma once

#include "avox/AvoxVideo.h"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace avox {

#define AVOX_IOS_VIDEOTOOLBOX_TIMEOUT_US 2000

class IOSVDecoder : public VideoDecoder {
public:
  IOSVDecoder();
  virtual ~IOSVDecoder();

private:
  VTDecompressionSessionRef decompressionSession = nullptr;
  CMVideoFormatDescriptionRef videoFormatDescription = nullptr;
  bool bMetalRender = false;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;
  // 流位深(H265 SPS profile_idc==2 即 Main10), 决定 VT 输出 8bit NV12 或 10bit x420
  int32_t streamBitDepth = 8;

  H264NalUnit h264Unit = {};
  H265NalUnit h265Unit = {};

  // VT 回调不保证显示序(头文件原文 not necessarily called in display order), 实测
  // 按投递序(解码序)吐帧, B 帧流的解码序 pts 倒跳, 直接上屏即画面往复。攒够
  // 重排深度后按 pts 放行最小者, 输出即显示序
  struct ReorderFrame {
    int64_t pts = 0;
    CVImageBufferRef buffer = nullptr;
  };
  std::vector<ReorderFrame> reorderBuf;
  // 回调在 VT 内部线程, flush/onClose/onInputEnd 在解码线程: 缓冲与深度都要互斥
  std::mutex reorderMutex;
  // 重排深度(帧) = 包级 max(pts-dts)/帧长; 0 表示无 B 帧, 纯直通不加延迟
  int64_t reorderFrames = 0;
  int64_t maxPtsMinusDts = 0;
  int64_t frameDurMs = 0;

  // VT 吃到坏 NAL 后会话永久 wedge(回调连环 BadData): 连击达阈值即扣帧等
  // 下个 IDR 重建会话自愈, 不让一次坏包打死整个硬解车道 (0926 seek 实证)
  std::atomic<int32_t> badDataStreak{0};
  std::atomic<bool> bWaitResyncIdr{false};
  int32_t resyncDropped = 0;

  // 按渲染方式把解码帧交给观察者; 消费掉传入的 buffer 引用
  void dispatchDecodedFrame(int64_t pts, CVImageBufferRef imageBuffer);
  // 攒够 reorderFrames 帧就放行当前最小 pts 者
  void drainReorder();
  // 输入排空: 按 pts 序放空剩余帧
  void flushReorder();
  // seek/关闭: 丢弃并释放扣住的帧
  void releaseReorder();

public:
  void updateYuvFormat();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;
  // 输入排空
  virtual void onInputEnd() override;

  // VideoDecoder
public:
  virtual void onClose() override;

private:
  static void decompressionOutputCallback(
      void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
      VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
      CMTime presentationTimeStamp, CMTime presentationDuration);
};

}
