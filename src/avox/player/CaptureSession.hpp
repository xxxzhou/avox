#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../AvoxAudio.h"
#include "../AvoxCapture.h"
#include "../module/JsonOption.hpp"
#include "../muxer/RawMuxer.hpp"
#include "../video/WindowRender.hpp"

namespace avox {

// 多输入采集会话: N 路视频各写各的文件, N 路音频各写各的 WAV, 共用一条会话时钟。
// 不复用 DeviceSource/RawSource(单源 + trackId=0), 输入 observer 直连;
// 录制链路 = WindowRender(纯格式转换, 离屏) + RawMuxer(每腿一个文件)。
// 失败语义: 源回调线程只落 failed 态不同步收尾(close 会 join 回调线程死锁),
// 清理等宿主 stop()/析构 —— 收到 onCaptureError/failed 后应调 stop()
class CaptureSession : public ICaptureSession, public JsonOption, public IMuxerOb {
 public:
  CaptureSession();
  virtual ~CaptureSession();

 protected:
  // 视频腿: 源回调线程直接 render+pushFrame(同步, 编码慢会反压采集节拍)
  struct VideoLeg : public IVideoSourceOb {
    IVideoSource* source = nullptr;
    std::string id;
    CaptureVideoParam rawParam = {};  // add 时的腿级参数(start 时再解析)
    std::string path;
    std::unique_ptr<WindowRender> convert = nullptr;
    std::unique_ptr<RawMuxer> muxer = nullptr;
    VideoDesc desc = {};
    // 首帧前 desc 未知(窗口腿): 首个 onVideoDesc 时回填并触发 muxer ready
    bool bMuxerReady = false;
    std::atomic<int64_t> frames{0};
    std::atomic<int64_t> missedTicks{0};  // 帧间 pts 缺口推导(采集侧跳拍)
    int64_t lastPts = 0;
    CaptureSession* session = nullptr;

    // IVideoSourceOb
    virtual void onVideoDesc(const VideoDesc& d) override;
    virtual void onVideoError(AVError err, const char* msg) override;
    virtual void onVideoFrame(const YUVFrame& frame) override;
    virtual void onGpuFrame(const GpuFrame& frame) override;
    virtual void onVideoClose() override;
    virtual void onCaptureInfo(const CaptureInfo& info) override;
  };

  // 音频腿: PCM 原样落 WAV(IWavSave), 不重采样不混音; 平面格式由 WavSave 转换
  struct AudioLeg : public IAudioSourceOb {
    IAudioSource* source = nullptr;
    std::string id;
    std::string path;
    std::unique_ptr<IWavSave> writer = nullptr;
    AudioDesc desc = {};
    int64_t startMs = -1;        // 首包会话时钟(与视频 pts 对齐用)
    int64_t lastPatchTick = 0;   // 上次头回填(5s 周期, 崩溃恢复)
    int64_t droppedBytes = 0;    // WAV 头未就绪期丢弃
    CaptureSession* session = nullptr;

    // IAudioSourceOb
    virtual void onAudioDesc(const AudioDesc& d) override;
    virtual void onAudioError(AVError err, const char* msg) override;
    virtual void onAudioFrame(const AvoxAFrame& frame) override;
    virtual void onAudioClose() override;
  };

 public:
  // ICaptureSession
  virtual bool addVideoInput(const CaptureVideoInput& in) override;
  virtual bool addAudioInput(const CaptureAudioInput& in) override;
  virtual bool setConfig(const CaptureConfig& cfg) override;
  virtual void setObserver(ICaptureSessionOb* ob) override;
  virtual IOption* getOption() override { return this; }
  virtual bool start(const char* dir) override;
  virtual void stop() override;
  virtual void pause() override;
  virtual void resume() override;
  virtual CaptureState getState() override;
  virtual CaptureStats getStats() override;
  virtual bool getCaptureGeometry(CaptureGeometry* geo) override;
  virtual int64_t getClockMs() override;
  virtual ISurfaceRender* getPreviewRender() override;

  // IMuxerOb: muxer open 回调里补输出规格并 ready(与 SourcePlayer::onMuxerOpen 同构)
  virtual void onMuxerOpen(MediaMuxer* muxer) override;
  virtual void onMuxerClose() override;

  // JsonOption: rec.* 长尾键(gop/profile/硬编开关), open 前设置生效
  virtual void onOptionChange(const char* key, ArgType option) override;

 protected:
  // 状态迁移(锁内); stopped/failed 为终态
  void setState(CaptureState newState);
  void notifyError(const char* id, AVError err, const char* msg);
  bool bRecording() const { return stateLoad == (int32_t)CaptureState::recording; }
  // 收尾(可重入): 关源 → 摘 observer → flush/关 muxer/WAV → 清空文件 → 元数据。
  // bFail=终态落 failed(不删失败现场的有效文件), 否则 stopped
  void stopInternal(bool bFail);
  // 会话时钟毫秒(已扣暂停)
  int64_t activeMs() const;
  // 会话元数据; bDraft=写 session.draft.json(启动即写, 宿主 stop 后移除)
  bool writeSessionInfo(const char* dir, bool bDraft);
  void removeDraft();
  // 零帧视频文件清理(只剩容器头的垃圾, 失败时不留)
  void cleanupEmptyFiles();
  // 每腿采集参数解析: fps 数值腿级优先回退会话级; 矩形回退仅显示器腿
  // (窗口腿尺寸首帧回报); 光标/边框是会话级口径
  CaptureVideoParam resolveLegParam(VideoLeg* leg) const;
  // 尺寸已知后下发编码描述并开闸(区域腿在 muxer open 时, 窗口腿在首个 desc)
  void legMuxerReady(VideoLeg* leg);
  void releaseDevices();
  VideoLeg* findLegByMuxer(MediaMuxer* muxer);

 protected:
  std::mutex stateMtx;
  std::mutex stopMtx;  // stopInternal 可重入防护
  std::atomic<int32_t> stateLoad{(int32_t)CaptureState::idle};
  CaptureState state = CaptureState::idle;
  ICaptureSessionOb* sessionOb = nullptr;
  CaptureConfig cfg = {};
  bool bConfigSet = false;
  bool bHardEncode = true;  // rec.hard.encode 长尾键覆盖(缺省=平台原生硬编)
  std::vector<std::unique_ptr<VideoLeg>> videos;
  std::vector<std::unique_ptr<AudioLeg>> audios;
  std::string outputDir;
  // 会话时钟
  int64_t t0Tick = 0;          // timeTick() 起点
  int64_t pausedAccumMs = 0;   // 暂停累计
  int64_t pauseStartTick = 0;
  std::atomic<int64_t> firstFrameMs{-1};  // 首帧的会话时钟(videoPts0Ms)
  CaptureGeometry geometry = {};
  bool bGeometrySet = false;
  std::string cursorMode = "baked";
  // 统计
  std::atomic<int64_t> statWindowStart{0};
  std::atomic<int64_t> statWindowFrames{0};
};

}
