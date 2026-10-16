#pragma once

#include <atomic>
#include <functional>
#include <utility>

#include "../codec/VideoDecoderChain.hpp"
#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "PtsFlattener.hpp"
#include "VideoDecoder.hpp"

namespace avox {

// 原VideoDecoder与MediaPlayer绑定比较深
// 主要开启线程从VideoTrack读取IO队列,解码给VideoTrack
// 原来与VideoTrack绑定的部分移到这里
class VDecoderTask : public IPlayerContext, public RunTask,
                     private VideoDecoderChain::Host {
 public:
  VDecoderTask();
  virtual ~VDecoderTask();

 private:
  // 车道链回调: 只做候选链不知道的事(接线/解绑/存配置/换道后处置)
  virtual bool laneInit(VideoDecoder &impl, const VCodecDesc &desc) override;
  virtual void laneClose(VideoDecoder &impl) override;
  virtual void laneSaveConfigs(VideoDecoder &oldImpl) override;
  virtual void laneChanged(VideoDecoder &newImpl, bool bFirst) override;
  VideoDecoder *decoder() { return chain.impl(); }

 protected:
  // 视频元信息
  VideoDesc srcDesc = {};
  VCodecId codecId = VCodecId::none;
  PBVideoInfo pbInfo = {};
  // 解码类型
  VCodecTh codecTh = VCodecTh::cpu;
  // 是否是硬解码
  bool bHardDecode = false;
  // 解码器允许的延迟,单位ms
  int32_t delayMs = 5000;
  // 重置标志位(切换软硬解等)
  bool bResetFlag = false;
  // 如果重置解码器，需要保持上个解码器里的配置帧
  std::vector<PacketBuf> configPackets;
  // 候选车道链: 选型与运行期换道(与抽帧腿/WebRTC 收流共用同一份策略)
  VideoDecoderChain chain;
  // 解码器已打开(success且已派发描述)
  bool bOpenDecode = false;
  // 视频轨是否真收到过帧: success会被重复参数集包空转返回污染, 看门狗只认实际产出
  bool bGotFrame = false;
  // 首帧看门狗基准: 只累计"解码器持续吃得到输入"的时长(连续供给满delayMs仍
  // 无帧才降级)。网络源打开阶段IO线程被参数探测占住不吐包(极空间直链实测
  // TrueHD探测~5s)属饿等, 清零重记——不然硬解车道总在数据洪流到达前一刻被误杀
  int64_t supplyMarkMs = 0;
  // 配置帧变化
  bool bDecodeUpdate = false;
  std::mutex configMutex;
  // seek重置: flush后解码器须整体重建(hw车道avcodec_flush_buffers清不掉
  // POC/frame_num, open-GOP恢复点落点会Frame num gap螺旋到0帧), 解码线程消费
  std::atomic<bool> bResetCtx{false};
  // MediaCodec 原地 flush 档(见flush注释): 保实例保 SurfaceTexture/EGL 链
  std::atomic<bool> bFlushCtx{false};
  // 老容器簇状畸形时间戳摊平(只影响送解码器的 pts, 不动容器与导出)
  PtsFlattener flattener;

 protected:
  class VideoTrack *trackContext = nullptr;

 public:
  bool start(class VideoTrack *trackContext);
  void setResetFlag() { bResetFlag = true; }
  void flush();
  void close();

 public:
  // 根据解码器类型选择渲染器
  RenderType selectRenderType();
  VideoDecoder *getDecoder() { return chain.impl(); }
  void addConfigRecord(ConfigAddType type);
  bool hardDecode() { return bHardDecode; }

  // RunTask
 protected:
  virtual void onRunTask() override;
};

}
