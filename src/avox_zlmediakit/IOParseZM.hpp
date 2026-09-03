#pragma once

#include <atomic>
#include <thread>

// #include "Player/MediaPlayer.h"
#include "avox/module/Json.hpp"
#include "avox/module/JsonOption.hpp"
#include "avox/source/AVSource.hpp"
#include "mk_mediakit.h"

namespace avox {

// Observer<IAVSourceOb>能转发回调到多个对象上
// ZL里的MediaPlayer主要是解析网络协议的功能，没有同步/渲染等播放功能
// mediakit::MediaPlayer本身回调转IOParseOb上
// MediaPlayer自身会开启socket线程，解协议
class IOParseZM : public AVSource {
public:
  IOParseZM();
  virtual ~IOParseZM();

protected:
  // mediakit::MediaPlayer::Ptr zlPlayer = nullptr;
  mk_player zlPlayer = nullptr;
  mk_frame_merger zlMerger = nullptr;
  // player 所在的 poller 线程，onClose 时用 mk_sync_do 同步释放，避免与 inputFrame 并发
  mk_thread zlPoller = nullptr;
  bool bFirstAudio = false;
  // close后ZLM线程仍可能短暂投递帧, onPacket靠它早退(跨线程读写)
  std::atomic<bool> bStop{false};
  // VOD点播流duration>0，可seek
  int64_t cduration = 0;
  // poller线程采样、player线程读取的统计缓存 (近似值, 允许稍旧, 故不加锁/不用atomic)
  // ZLMediaKit 的 loss_rate/progress/position 内部读 _rtcp_context/_demuxer,
  // 仅在 poller 线程访问安全; 在 onPacket(poller) 里采样, getter 只读缓存, 不再跨线程裸读
  float cLossRateVideo{0.0f};
  float cLossRateAudio{0.0f};
  float cProgress{0.0f};
  int64_t cPosition{0};
    // 创建本 IOParseZM 的线程（player 主线程），用于把 ZM socket 线程归属到所属 TaskTrack
  std::thread::id parentTid;
  // seek后包pts重构: 服务器seek后时间戳失真, 用seek目标重锚
  int64_t seekAbsTarget = -1;            // seek目标绝对PTS, -1=没seek过
  int64_t seekPtsOffset[2] = {0, 0};     // video/audio 各自偏移
  bool seekPtsPending[2] = {false, false};

protected:
  void onResume();
  bool onPacket(mk_frame frame);
  // poller线程: 采样丢包率/进度/位置到缓存, 供 player 线程 getter 无锁读取
  void sampleStats();
  // seek后重构包pts/dts (video/audio各自锚定到seek目标)
  void rewriteSeekPts(int64_t& pts, int64_t& dts, PackType packtype);

public:
  // 初始化，打开文件/网络流
  virtual bool onOpen() override;
  // 关闭
  virtual void onClose() override;
  // 暂停
  virtual void pause(bool bFlag) override;
  // 能seek吗?
  virtual SeekType seekType() const override;
  // 0-1
  virtual void seekTo(double progress) override;
  // 毫秒时间
  virtual bool seekTo(int64_t pos) override;
  // 返回总时长，单位毫秒
  virtual int64_t duration() const override;
  // 返回进度，0~1
  virtual double progress() const override;
  // 返回绝对时间的播放时间，单位毫秒
  virtual int64_t position() const override;

public:
  virtual bool vaild() override { return !bStop; }
  virtual void onSpeed() override;
  // 获取丢包率 (RTSP/RTP 有效)
  virtual float getLossRate(TrackType type) override;
};

}