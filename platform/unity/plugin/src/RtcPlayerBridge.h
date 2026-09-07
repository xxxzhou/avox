#pragma once

#include "AvoxSdk.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <stdint.h>

// 轮询事件 (avox线程入队, Unity主线程poll), 对应 AvoxNative.NativeRtcEvent 布局
struct AvoxRtcEvent {
  enum class EType : int32_t {
    none = 0,
    state = 1,         // state = avox::PlayerState
    ready = 2,
    error = 4,         // code = AVError/DecodeResult, msg = 文本
    connState = 5,     // state = avox::RtcConnState
    firstFrame = 6,
    localSdp = 7,      // SDP 经 localSdp() 拉取 (超 msg 容量)
    iceCandidate = 8,  // msg = candidate, mid = mid, code = mlineIndex
  };
  int32_t type = 0;
  int32_t state = 0;
  int32_t code = 0;
  int32_t reserved = 0;
  char msg[240] = {0};
  char mid[64] = {0};
};

// RTC 桥注册表查找: id → bridge (纹理更新回调 CPU 路径第三级查找)
class RtcPlayerBridge;
RtcPlayerBridge* findRtcBridge(uint32_t id);

// ICE 服务器配置缓存 (IRtcPlayer::addIceServer 参数都是 const char*, 不存 avox 类型)
struct RtcIceServerCfg {
  std::string url;
  std::string user;
  std::string pwd;
};

// avox IRtcPlayer 包装 + 远端画面帧桥接 (对应 PlayerBridge, 纹理机制共用)
//
// 当前恒 CPU 路径: setOffSurface + enableYuvOut(nv12), 渲染线程 onFrame 转 BGRA
// 进帧槽, Unity 经 IssuePluginCustomTextureUpdateV2 回调取帧
// (avoxTextureUpdateCallback → findRtcBridge 第三级查找)。
// GPU 直通 (Windows Vulkan 导入/D3D11/D3D12 拷贝) 为二期: 移植 PlayerBridge 的
// startGpuImport/updateGpu/renderDx11Copy 并把 gpuMode() 打开即可, C# 侧逻辑同 AvoxPlayer。
class RtcPlayerBridge : public avox::IMediaPlayerOb,
                        public avox::IRtcEventOb,
                        public avox::ISurfaceRenderOb {
 public:
  explicit RtcPlayerBridge(uint32_t id);
  ~RtcPlayerBridge() override;

  uint32_t id() const { return playerId; }

  // ── 配置 (open 前调用; 播放器未建时缓存, createPlayer 时统一应用) ──
  void setRollType(int32_t type);
  void setVideoDirection(int32_t dir);
  void setAudioDirection(int32_t dir);
  void addIceServer(const char* url, const char* user, const char* pwd);
  void setAutoReconnect(bool bEnable, int32_t retries);
  void setSendVideoBitrate(int32_t kbps);
  void setSendVideoFps(int32_t fps);
  void setPreferredVideoCodec(const char* codec);
  void setEnableDataChannel(bool bEnable);
  void setVolume(float volume);

  // ── 连接控制 ──
  // HTTP 信令 (ZLM 测试信令/WHEP, Offer 拉流主路径): 创建 agent 并 open,
  // 本地 SDP 自动 POST, 远端 answer 自动回填
  bool connectSignaling(const char* url);
  // 自定义信令: open 后监听 localSdp/iceCandidate 事件自己送出
  void openRtc();
  void close();
  // 手动重连 (重建 PeerConnection 并重新走信令)
  void reconnect();

  // ── 信令回填 (自定义信令) ──
  void setRemoteSdp(const char* sdp);
  void addIceCandidate(const char* candidate, const char* mid, int32_t mline);

  // ── DataChannel ──
  bool sendDataChannel(const uint8_t* data, int32_t size);

  // ── 查询 (主线程直调) ──
  int32_t state() const { return stateCache.load(); }
  int32_t connectionState() const;
  double fps() const;
  float lossRate() const;
  int32_t rttMs() const;
  // 事件弹出一条, 无事件返回 false
  bool pollEvent(AvoxRtcEvent* out);
  // 本地 SDP (localSdp 事件后拉取; 写入 buf 含\0, 缓冲不足截断, 返回长度)
  int32_t localSdp(char* buf, int32_t bufSize);
  // 弹出一条 DataChannel 消息 (返回字节数, 0=无; buf 不足丢弃超出部分)
  int32_t pollDataChannel(uint8_t* buf, int32_t bufSize);
  // 远端帧尺寸, 无帧返回 false
  bool frameInfo(int32_t* w, int32_t* h);
  // 是否 GPU 直通 (当前恒 false, 二期接 GPU 后打开)
  bool gpuMode() const { return false; }

  // 纹理更新回调 (渲染线程): 从帧槽供 BGRA (avoxTextureUpdateCallback 第三级查找)
  bool allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData);

 private:
  // ── avox::IRtcPlayerOb (avox 线程; 只继承 IRtcPlayerOb, 状态回调一并携带) ──
  void onStateChange(avox::PlayerState preState, avox::PlayerState state) override;
  void onReady() override;
  void onIoError(avox::AVError error, const char* msg) override;
  void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override;
  void onConnectionState(avox::RtcConnState state) override;
  void onFirstVideoFrame() override;
  void onDataChannelMsg(const char* data, int32_t size) override;

  // ── avox::IRtcEventOb: 本地SDP/ICE → 事件 (信令线程回调) ──
  void onLocalSdp(const char* sdp) override;
  void onIceCandidate(const char* candidate, const char* mid,
                      int mlineIndex) override;

  // ── avox::ISurfaceRenderOb (avox 渲染线程) ──
  void onFrame(const avox::YUVFrame& frame) override;
  void onWinSizeChange(int32_t width, int32_t height) override;

  void createPlayer();
  void destroyPlayer();
  void bindSurface();
  void unbindSurface();
  void pushEvent(const AvoxRtcEvent& e);
  void pushError(int32_t code, const char* msg);

  uint32_t playerId;
  avox::IRtcPlayer* player = nullptr;
  // 内置 ZLM/WHEP 信令观察者 (connectSignaling 模式创建)
  avox::IRtcEventOb* sdpAgent = nullptr;
  avox::ISurfaceRender* surface = nullptr;
  avox::ColorSpaceDesc colorSpace;

  // ── 配置缓存 (open 前设置) ──
  int32_t rollType = 0;
  int32_t videoDir = 3;
  int32_t audioDir = 3;
  int32_t maxVideoBitrateKbps = 0;
  int32_t maxVideoFps = 0;
  bool bEnableDataChannel = false;
  bool bAutoReconnect = true;
  int32_t maxReconnectRetries = 3;
  float volume = 1.0f;
  std::string preferredCodec;
  std::vector<RtcIceServerCfg> iceServers;

  std::atomic<int> stateCache{0};

  // 事件队列 (avox线程入队, 主线程 poll)
  std::mutex eventMutex;
  std::deque<AvoxRtcEvent> events;
  // 本地 SDP 快照 (localSdp 事件后 C# 拉取)
  std::mutex sdpMutex;
  std::string localSdpStr;
  // DataChannel 收包队列 (信令线程入队, 主线程 poll)
  std::mutex dcMutex;
  std::deque<std::vector<uint8_t>> dcQueue;
  // CPU 帧槽 (渲染线程写, 回调线程读)
  std::mutex frameMutex;
  std::vector<uint8_t> frameBgra;
  int32_t frameW = 0;
  int32_t frameH = 0;
};
