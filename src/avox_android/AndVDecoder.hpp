#pragma once
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <media/NdkMediaCodec.h>
#include <deque>
#include <queue>

#include "AndCommon.hpp"
#include "JniSurfaceTexture.hpp"
#include "avox/video/VideoDecoder.hpp"
#include "avox_egl/GLESContext.hpp"

namespace avox {

#define AVOX_ANDROID_MEDIACODEC_TIMEOUT_US 2000

#if __ANDROID_API__ >= 21

class AndVDecoder : public VideoDecoder, public GLESContext {
public:
  AndVDecoder();
  virtual ~AndVDecoder();

private:
  AMediaCodec *mediaCodec = nullptr;
  AMediaFormat *format = nullptr;
  // 解码后的数据，解码后直接输出到对应OES纹理
  std::unique_ptr<JniSurfaceTexture> surfaceTexture = nullptr;  
  // 是否直接把解码数据渲染到OES纹理，不经过CPU
  bool bOpenglRender = false;
  // 是否打开
  bool bOpen = false;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;
  int32_t sliceHeight = 0;

  std::atomic<bool> bFrameAvailable = false;

  // 可能有B帧，需要自己处理
  // std::priority_queue<GpuFrame, std::vector<GpuFrame>, GpuFrameSortPTS>
  //     sortQueue;
  // size_t FRAME_QUEUE_THRESHOLD = 5;

  // 按 codecDesc 建 codec+format(onVaild 与 seek复位重建共用)
  bool createCodec();

  // input buffer 耗尽时的待喂包深拷贝队列: 直接丢包=缺slice宏块花屏(9/25 真机)
  std::deque<std::vector<uint8_t>> pendingPackets;
  std::deque<int64_t> pendingPts;

  // 车道守卫(只在"首帧交付前"生效, bLaneOut 一旦为真整体停用): 驱动在 STARTED
  // 之后才翻脸(收下 csd 报 unrecoverable error→MediaCodec 进 Released)时,
  // configure/start 都是成功, 上层 openFailed 换道口不响、共享看门狗又被供给
  // 判据挡住 ⇒ 只表现为入队恒被拒 + 零输出 + 单核空转。这里在本层把"车道已死"
  // 归成 openFailed, 复用车道换道逻辑让软解接手。
  // 触发条件全部选健康车道不可能产生的量, 见 checkLaneAlive 注释
  bool bLaneDead = false;
  bool bLaneOut = false;       // 已交付过输出: 守卫停用(避免误伤正常播放)
  int32_t laneQueueFails = 0;  // 连续入队失败(硬错)
  int32_t laneDequeErrs = 0;   // 连续 dequeue 硬错(非"稍后重试")
  int32_t laneRefused = 0;     // 连续被拒次数, 只随供给量增长(不看墙钟)
  int64_t laneStartMs = 0;     // 首帧交付前计时起点
  int64_t laneFedFrames = 0;   // 已成功入队帧数(取证)
  bool bLaneErrLogged = false; // 首个负返回值取证(只打一次)
  void resetLaneCheck();
  DecodeResult checkLaneAlive();

public:
  void updateYuvFormat();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket &packet) override;
  // flush
  virtual void flush() override;

  // VideoDecoder
public:
  virtual void onClose() override;

public:
  // Render为true表明MediaCodec队列的数据压入到OES纹理
  // 释放纹理
  virtual void onFrameRelease(bool bRender, const GpuFrame &frame) override;
};

#endif
}
