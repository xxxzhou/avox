#include "CaptureSession.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>

#include "../Avox.hpp"
#include "../AvoxTime.h"
#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../module/OptionKey.hpp"
#include "../source/AudioSource.hpp"
#include "../source/VideoSource.hpp"
#include "../subtitle/CharsetConvert.hpp"

namespace avox {

namespace {

// 音频设备占用表(视频走 VideoSource.hpp 的跨模块登记, 平台刷新要 pin)
std::mutex g_audioDevMtx;
std::set<void*> g_busyAudioDevices;

std::string escapeJson(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

// WAV 头周期回填间隔(崩溃恢复: 被杀最多丢最后 5s 的长度字段)
constexpr int64_t kWavPatchIntervalTick = 5LL * 10000000LL;

}  // namespace

const char* getCaptureStateStr(CaptureState state) {
  switch (state) {
#define XX(name, value, str) \
  case CaptureState::name:  \
    return str;
    AVOX_MAP_CAPTURE_STATE(XX)
#undef XX
    default:
      return "invalid";
  }
}

CaptureSession::CaptureSession() {}

CaptureSession::~CaptureSession() {
  stop();
  releaseDevices();
}

// ========== 输入与配置 ==========

bool CaptureSession::addVideoInput(const CaptureVideoInput& in) {
  std::lock_guard<std::mutex> lock(stateMtx);
  if (state != CaptureState::idle && state != CaptureState::stopped &&
      state != CaptureState::failed) {
    LOGFLF(LogLevel::warn, "addVideoInput ignored, state:", getCaptureStateStr(state));
    return false;
  }
  if (!in.source || !in.id) {
    return false;
  }
  if (!dynamic_cast<VideoSource*>(in.source)) {
    LOGFLF(LogLevel::error, "video source is not a VideoSource:", in.id);
    return false;
  }
  // 占用登记(跨模块): 同一源被两个会话同时 open 会互相覆盖内部采集资源
  if (!markVideoSourceBusy(in.source)) {
    LOGFLF(LogLevel::error, "video source busy in another session:", in.id);
    return false;
  }
  auto leg = std::make_unique<VideoLeg>();
  leg->id = in.id;
  leg->source = in.source;
  leg->rawParam = in.param;
  leg->session = this;
  videos.push_back(std::move(leg));
  return true;
}

bool CaptureSession::addAudioInput(const CaptureAudioInput& in) {
  std::lock_guard<std::mutex> lock(stateMtx);
  if (state != CaptureState::idle && state != CaptureState::stopped &&
      state != CaptureState::failed) {
    return false;
  }
  if (!in.source || !in.id) {
    return false;
  }
  if (!dynamic_cast<AudioSource*>(in.source)) {
    LOGFLF(LogLevel::error, "audio source is not an AudioSource:", in.id);
    return false;
  }
  {
    std::lock_guard<std::mutex> devLock(g_audioDevMtx);
    if (g_busyAudioDevices.count(in.source)) {
      LOGFLF(LogLevel::error, "audio source busy in another session:", in.id);
      return false;
    }
    g_busyAudioDevices.insert(in.source);
  }
  auto leg = std::make_unique<AudioLeg>();
  leg->id = in.id;
  leg->source = in.source;
  leg->session = this;
  audios.push_back(std::move(leg));
  return true;
}

bool CaptureSession::setConfig(const CaptureConfig& config) {
  std::lock_guard<std::mutex> lock(stateMtx);
  if (state == CaptureState::starting || state == CaptureState::recording ||
      state == CaptureState::paused) {
    LOGFLF(LogLevel::warn, "setConfig ignored while capturing");
    return false;
  }
  cfg = config;
  bConfigSet = true;
  return true;
}

void CaptureSession::setObserver(ICaptureSessionOb* ob) { sessionOb = ob; }

// ========== 启停 ==========

bool CaptureSession::start(const char* dir) {
  if (stateLoad != (int32_t)CaptureState::idle &&
      stateLoad != (int32_t)CaptureState::stopped &&
      stateLoad != (int32_t)CaptureState::failed) {
    LOGFLF(LogLevel::warn, "start ignored, state:", getCaptureStateStr(state));
    return false;
  }
  if (!dir || videos.empty()) {
    LOGFLF(LogLevel::error, "start failed: dir or video input missing");
    return false;
  }
  outputDir = dir;
  // 每腿参数解析与失败口径: primary 决定几何/时钟, 打不开即会话失败;
  // 第二路视频/音轨失败降级继续(只报 onCaptureError)
  VideoLeg* primary = videos[0].get();
  CaptureVideoParam primaryParam = resolveLegParam(primary);
  if (primary->source->getDeviceKind() == VDeviceKind::monitor &&
      primaryParam.regionW <= 0) {
    LOGFLF(LogLevel::error,
           "start failed: capture region required (virtual-screen abs pixels)");
    return false;
  }
  // 重新打占用标: stop 清过, 失败态未 stop 时残留的也是本会话的, 先清再打
  releaseDevices();
  for (auto& leg : videos) {
    if (!markVideoSourceBusy(leg->source)) {
      LOGFLF(LogLevel::error, "start failed: source busy in another session");
      return false;
    }
  }
  {
    std::lock_guard<std::mutex> devLock(g_audioDevMtx);
    for (auto& leg : audios) {
      if (!g_busyAudioDevices.insert(leg->source).second) {
        LOGFLF(LogLevel::error, "start failed: audio busy in another session");
        return false;
      }
    }
  }
  setState(CaptureState::starting);
  t0Tick = timeTick();
  pausedAccumMs = 0;
  firstFrameMs = -1;
  geometry = {};
  bGeometrySet = false;
  cursorMode = "baked";
  std::error_code ec;
  std::filesystem::create_directories(outputDir, ec);
  std::filesystem::create_directories(outputDir + "/audio", ec);
  const char* container = cfg.container ? cfg.container : "mkv";
  // 第一遍: 每腿参数/desc 预置(显示器腿尺寸已知, 窗口腿等首个 desc)与路径
  for (auto& leg : videos) {
    CaptureVideoParam lp = resolveLegParam(leg.get());
    double fps = lp.fps > 0 ? lp.fps
                            : (cfg.outVideo.fps > 0 ? cfg.outVideo.fps : 30.0);
    leg->desc = {};
    leg->desc.fps = fps;
    leg->desc.colorSpace = ColorSpaceDesc{YuvStandard::bt601, YuvRange::full};
    if (cfg.outVideo.width > 0) {
      leg->desc.width = cfg.outVideo.width;
      leg->desc.height = cfg.outVideo.height;
    } else if (leg->source->getDeviceKind() == VDeviceKind::monitor &&
               lp.regionW > 0 && lp.regionH > 0) {
      // 窗口腿不预置(WGC 窗口路不裁剪, 尺寸=窗口内容, 首帧回报)
      leg->desc.width = lp.regionW & ~1;
      leg->desc.height = lp.regionH & ~1;
    }
    leg->path = outputDir + "/" + leg->id + "." + container;
    leg->bMuxerReady = false;
    leg->frames = 0;
    leg->missedTicks = 0;
    leg->lastPts = 0;
  }
  for (auto& leg : audios) {
    leg->writer = nullptr;
    leg->desc = {};
    leg->startMs = -1;
    leg->droppedBytes = 0;
    leg->lastPatchTick = 0;
    // 计划路径先落 json(desc 没来/静音期缺席也可见)
    leg->path = outputDir + "/audio/" + leg->id + ".wav";
  }
  // 启动即写 draft(进程被杀时宿主凭它恢复现场); stop 成功后移除
  writeSessionInfo(outputDir.c_str(), true);
  // 第二遍: 建转换腿与 muxer(先 muxer 后源, 首帧即入流)
  for (auto& leg : videos) {
    leg->convert = std::make_unique<WindowRender>();
    // 离屏; yuv 输出在 onMuxerOpen 按硬/软编定(同 SourcePlayer::onMuxerOpen)
    leg->convert->setOffSurface(YuvType::other);
    if (cfg.outVideo.width > 0) {
      // 会话级缩放: 源尺寸(窗口腿未知, 显示器腿=矩形)统一到 outVideo
      if (leg->source->getDeviceKind() != VDeviceKind::monitor ||
          leg->desc.width != (resolveLegParam(leg.get()).regionW & ~1)) {
        leg->convert->enableSizeChange(cfg.outVideo.width, cfg.outVideo.height);
      }
    }
    leg->muxer = std::make_unique<RawMuxer>();
    leg->muxer->setMuxerType(MuxerType::ffmpeg);
    leg->muxer->setVideoCodec(cfg.videoCodec);
    // 本期音轨不进容器(分轨 WAV, finalize 合并)
    leg->muxer->setAudioCodec(ACodecId::none);
    leg->muxer->setHardEncode(bHardEncode);
    VideoDesc outDesc = leg->desc;
    outDesc.type = YuvType::nv12;
    // fps>0 → muxer 按它门控丢帧 + 同步给编码器
    leg->muxer->setVideoDesc(outDesc);
    leg->muxer->setVideoEncodeParam(cfg.videoEncode);
    leg->muxer->setContext(this);
    if (!leg->muxer->open(leg->path.c_str())) {
      LOGFLF(LogLevel::error, "muxer open failed:", leg->path);
      notifyError(leg->id.c_str(), AVError::other, "muxer open failed");
      stopInternal(true);
      return false;
    }
  }
  // 音频先开: onAudioDesc 到达即建 WAV(头占位), 之后 PCM 无头丢弃期最短
  for (auto& leg : audios) {
    if (auto* source = dynamic_cast<AudioSource*>(leg->source)) {
      source->addObserver(leg.get());
      if (!leg->source->open()) {
        LOGFLF(LogLevel::warn, "audio source open failed:", leg->id);
        notifyError(leg->id.c_str(), AVError::deviceError, "audio open failed");
      }
    }
  }
  // 视频后开: muxer 已 recording, 首帧即入流
  for (size_t i = 0; i < videos.size();) {
    VideoLeg* leg = videos[i].get();
    leg->source->setCaptureParam(resolveLegParam(leg));
    if (auto* source = dynamic_cast<VideoSource*>(leg->source)) {
      source->addObserver(leg);
    }
    if (!leg->source->open()) {
      if (i == 0) {
        LOGFLF(LogLevel::error, "primary video open failed:", leg->id);
        stopInternal(true);
        return false;
      }
      // 第二路视频失败: 降级继续(移除本腿, 释放占用)
      LOGFLF(LogLevel::warn, "secondary video open failed, degrade:", leg->id);
      notifyError(leg->id.c_str(), AVError::deviceError,
                  "video open failed, track dropped");
      // 先摘 observer: leg 随后随 erase 析构, 源侧留裸指针即野指针
      if (auto* source = dynamic_cast<VideoSource*>(leg->source)) {
        source->removeObserver(leg);
      }
      unmarkVideoSourceBusy(leg->source);
      // muxer 在源 open 之前已建容器; erase 后 cleanupEmptyFiles 只遍历 videos
      // 看不到它, 先关(析构 close)再删, 不留零帧孤儿文件
      leg->muxer = nullptr;
      leg->convert = nullptr;
      std::error_code rmEc;
      std::filesystem::remove(leg->path, rmEc);
      if (rmEc) {
        LOGFLF(LogLevel::warn, "remove degraded leg file failed:", leg->path,
               " err:", rmEc.value());
      }
      videos.erase(videos.begin() + i);
      continue;
    }
    i++;
  }
  statWindowStart = timeTick();
  statWindowFrames = 0;
  // 启动期间源线程可能已报错落 failed(首帧看门狗/设备错误): 不翻回 recording
  if (stateLoad != (int32_t)CaptureState::starting) {
    LOGFLF(LogLevel::error, "capture failed during start");
    stopInternal(true);
    return false;
  }
  setState(CaptureState::recording);
  LOGFLF(LogLevel::info, "capture session recording:", outputDir,
         " videos:", videos.size(), " audios:", audios.size());
  return true;
}

void CaptureSession::stop() { stopInternal(false); }

void CaptureSession::stopInternal(bool bFail) {
  std::lock_guard<std::mutex> stopLock(stopMtx);
  CaptureState cur = (CaptureState)stateLoad.load();
  if (cur != CaptureState::recording && cur != CaptureState::paused &&
      cur != CaptureState::starting && cur != CaptureState::failed) {
    return;
  }
  setState(CaptureState::stopping);
  // 顺序: 关源(回调线程先停) → 摘 observer → 收尾 muxer/WAV → 元数据
  for (auto& leg : videos) {
    if (leg->source) {
      leg->source->close();
    }
  }
  for (auto& leg : audios) {
    if (leg->source) {
      leg->source->close();
    }
  }
  for (auto& leg : videos) {
    if (auto* source = dynamic_cast<VideoSource*>(leg->source)) {
      source->removeObserver(leg.get());
    }
  }
  for (auto& leg : audios) {
    if (auto* source = dynamic_cast<AudioSource*>(leg->source)) {
      source->removeObserver(leg.get());
    }
  }
  for (auto& leg : videos) {
    if (leg->muxer) {
      leg->muxer->close();
      leg->muxer = nullptr;
    }
    leg->convert = nullptr;
  }
  for (auto& leg : audios) {
    if (leg->writer && leg->writer->bOpen()) {
      leg->writer->close();
    }
  }
  cleanupEmptyFiles();
  writeSessionInfo(outputDir.c_str(), false);
  removeDraft();
  releaseDevices();
  setState(bFail ? CaptureState::failed : CaptureState::stopped);
}

void CaptureSession::pause() {
  if (stateLoad != (int32_t)CaptureState::recording) {
    return;
  }
  pauseStartTick = timeTick();
  setState(CaptureState::paused);
}

void CaptureSession::resume() {
  if (stateLoad != (int32_t)CaptureState::paused) {
    return;
  }
  pausedAccumMs += (timeTick() - pauseStartTick) / 10000;
  setState(CaptureState::recording);
}

void CaptureSession::releaseDevices() {
  for (auto& leg : videos) {
    unmarkVideoSourceBusy(leg->source);
  }
  std::lock_guard<std::mutex> devLock(g_audioDevMtx);
  for (auto& leg : audios) {
    g_busyAudioDevices.erase(leg->source);
  }
}

// ========== 查询 ==========

CaptureState CaptureSession::getState() { return state; }

CaptureStats CaptureSession::getStats() {
  CaptureStats st = {};
  int64_t now = timeTick();
  int64_t winStart = statWindowStart.load();
  int64_t winFrames = statWindowFrames.exchange(0);
  double sec = (double)(now - winStart) / 1e7;
  if (sec > 0.2) {
    st.achievedFps = (double)winFrames / sec;
    statWindowStart.store(now);
  } else {
    // 窗口太短不结窗, 累计回下个窗口
    statWindowFrames += winFrames;
  }
  for (auto& leg : videos) {
    st.pushedFrames += leg->frames.load();
    if (leg->muxer) {
      st.droppedBySize += leg->muxer->getDroppedBySize();
      st.droppedByFpsGate += leg->muxer->getDroppedByFpsGate();
      st.encoderDropped += leg->muxer->getEncoderDropped();
    }
  }
  if (!videos.empty()) {
    st.missedTicks = videos[0]->missedTicks.load();
  }
  st.elapsedMs = activeMs() > 0 ? activeMs() : 0;
  for (auto& leg : audios) {
    if (leg->writer) {
      st.bytesWritten += leg->writer->getDataBytes();
    }
  }
  return st;
}

bool CaptureSession::getCaptureGeometry(CaptureGeometry* geo) {
  if (!geo) {
    return false;
  }
  if (bGeometrySet) {
    *geo = geometry;
    return true;
  }
  // 首帧前: 用 primary 生效矩形估(偶对齐), 与源实际生效值可能差奇数尾列;
  // 窗口腿无矩形时无从估计(宿主可把窗口矩形填进 CaptureVideoInput.param)
  if (videos.empty()) {
    return false;
  }
  CaptureVideoParam lp = resolveLegParam(videos[0].get());
  geo->x = lp.regionX;
  geo->y = lp.regionY;
  geo->width = lp.regionW & ~1;
  geo->height = lp.regionH & ~1;
  return lp.regionW > 0;
}

int64_t CaptureSession::getClockMs() {
  if (t0Tick == 0) {
    return -1;
  }
  return activeMs();
}

ISurfaceRender* CaptureSession::getPreviewRender() {
  return videos.empty() ? nullptr : videos[0]->convert.get();
}

int64_t CaptureSession::activeMs() const {
  if (t0Tick == 0) {
    return -1;
  }
  int64_t ms = (timeTick() - t0Tick) / 10000 - pausedAccumMs;
  if (stateLoad == (int32_t)CaptureState::paused) {
    ms -= (timeTick() - pauseStartTick) / 10000;
  }
  return ms;
}

// ========== 状态与回调 ==========

void CaptureSession::setState(CaptureState newState) {
  CaptureState pre = CaptureState::idle;
  {
    std::lock_guard<std::mutex> lock(stateMtx);
    pre = state;
    state = newState;
    stateLoad = (int32_t)newState;
  }
  LOGFLF(LogLevel::info, "capture state:", getCaptureStateStr(pre), "->",
         getCaptureStateStr(newState));
  if (sessionOb && pre != newState) {
    sessionOb->onCaptureState(pre, newState);
  }
}

void CaptureSession::notifyError(const char* id, AVError err, const char* msg) {
  LOGFLF(LogLevel::error, "capture error [", id ? id : "?", "]:",
         msg ? msg : "");
  if (sessionOb) {
    sessionOb->onCaptureError(id, err, msg);
  }
}

CaptureSession::VideoLeg* CaptureSession::findLegByMuxer(MediaMuxer* muxer) {
  for (auto& leg : videos) {
    if (leg->muxer.get() == muxer) {
      return leg.get();
    }
  }
  return nullptr;
}

void CaptureSession::legMuxerReady(VideoLeg* leg) {
  if (leg->bMuxerReady || !leg->muxer) {
    return;
  }
  VTrackDesc vdesc = {};
  vdesc.desc = leg->desc;
  leg->muxer->setInVideoDesc(vdesc);
  leg->muxer->ready();
  leg->bMuxerReady = true;
  LOGFLF(LogLevel::info, "capture muxer ready:", leg->path, " ", leg->desc);
}

CaptureVideoParam CaptureSession::resolveLegParam(VideoLeg* leg) const {
  CaptureVideoParam p = leg->rawParam;
  if (p.fps <= 0) {
    p.fps = cfg.video.fps;
  }
  // 矩形回退仅显示器腿: 窗口腿继承会话矩形没有意义(WGC 窗口路不裁剪)
  if (leg->source->getDeviceKind() == VDeviceKind::monitor &&
      (p.regionW <= 0 || p.regionH <= 0)) {
    p.regionX = cfg.video.regionX;
    p.regionY = cfg.video.regionY;
    p.regionW = cfg.video.regionW;
    p.regionH = cfg.video.regionH;
  }
  // 光标/边框是会话级口径(关光标/去黄框是录屏统一行为), 不做腿级覆盖
  p.captureCursor = cfg.video.captureCursor;
  p.border = cfg.video.border;
  return p;
}

void CaptureSession::onMuxerOpen(MediaMuxer* muxer) {
  VideoLeg* leg = findLegByMuxer(muxer);
  if (!leg) {
    return;
  }
  bool bHard = leg->muxer->getHardEncode();
  // 硬编 NV12 / 软编 YUV420P(同 SourcePlayer::onMuxerOpen 口径)
  if (bHard) {
    leg->convert->enableYuvOut(YuvType::nv12);
    leg->desc.type = YuvType::nv12;
  } else {
    leg->convert->enableYuvOut(YuvType::yuv420P);
    leg->desc.type = YuvType::yuv420P;
  }
  leg->convert->setColorSpace(leg->desc.colorSpace);
  if (leg->desc.width > 0) {
    // 区域腿: 尺寸已预置, 直接开闸
    legMuxerReady(leg);
  }
  // 窗口腿: 尺寸等首个 onVideoDesc, 在那开闸
}

void CaptureSession::onMuxerClose() {}

// ========== 视频腿回调(源线程) ==========

void CaptureSession::VideoLeg::onVideoDesc(const VideoDesc& d) {
  LOGFLF(LogLevel::info, "capture video desc [", id, "]:", d.width, "x",
         d.height, " fps:", d.fps);
  if (!session || bMuxerReady) {
    return;
  }
  if (desc.width <= 0 && d.width > 1 && d.height > 1) {
    // 窗口腿: 尺寸首帧才知道(偶对齐与 WGC 输出口径一致)
    desc.width = d.width & ~1;
    desc.height = d.height & ~1;
  }
  session->legMuxerReady(this);
}

void CaptureSession::VideoLeg::onVideoError(AVError err, const char* msg) {
  if (!session) {
    return;
  }
  session->notifyError(id.c_str(), err, msg);
  // 源回调线程里不能同步收尾(stopInternal 的 close 会 join 本线程死锁):
  // 只落失败态, 清理等宿主 stop()/析构(契约见 ICaptureSession 注释)
  if (err != AVError::none &&
      session->stateLoad == (int32_t)CaptureState::recording) {
    session->setState(CaptureState::failed);
  }
}

void CaptureSession::VideoLeg::onVideoFrame(const YUVFrame& frame) {
  if (!session || !convert || !muxer || !bMuxerReady || !session->bRecording()) {
    return;
  }
  if (session->firstFrameMs.load() < 0) {
    session->firstFrameMs.store(session->activeMs());
  }
  // 暂停压实: 源网格 pts 含暂停窗, 扣掉暂停累计让文件时间线连续
  YUVFrame adjusted = frame;
  adjusted.pts -= session->pausedAccumMs;
  convert->render(adjusted);
  convert->pushFrame(muxer.get());
  frames++;
  session->statWindowFrames++;
  // 帧间 pts 缺口 = 采集侧跳拍(慢帧), 供统计暴露
  double periodMs = desc.fps > 0 ? 1000.0 / desc.fps : 33.4;
  if (lastPts > 0 && adjusted.pts > lastPts) {
    int64_t gap = (int64_t)((adjusted.pts - lastPts) / periodMs + 0.5);
    if (gap > 1) {
      missedTicks += gap - 1;
    }
  }
  lastPts = adjusted.pts;
}

void CaptureSession::VideoLeg::onGpuFrame(const GpuFrame& frame) {
  if (!session || !convert || !muxer || !bMuxerReady || !session->bRecording()) {
    return;
  }
  if (session->firstFrameMs.load() < 0) {
    session->firstFrameMs.store(session->activeMs());
  }
  GpuFrame adjusted = frame;
  adjusted.pts -= session->pausedAccumMs;
  adjusted.dts = adjusted.pts;
  convert->render(adjusted);
  convert->pushFrame(muxer.get());
  frames++;
  session->statWindowFrames++;
  double periodMs = desc.fps > 0 ? 1000.0 / desc.fps : 33.4;
  if (lastPts > 0 && adjusted.pts > lastPts) {
    int64_t gap = (int64_t)((adjusted.pts - lastPts) / periodMs + 0.5);
    if (gap > 1) {
      missedTicks += gap - 1;
    }
  }
  lastPts = adjusted.pts;
}

void CaptureSession::VideoLeg::onVideoClose() {}

void CaptureSession::VideoLeg::onCaptureInfo(const CaptureInfo& info) {
  if (!session) {
    return;
  }
  session->cursorMode = info.cursorMode ? info.cursorMode : "baked";
  if (info.regionW > 0 && info.regionH > 0) {
    session->geometry.x = info.regionX;
    session->geometry.y = info.regionY;
    session->geometry.width = info.regionW;
    session->geometry.height = info.regionH;
    session->bGeometrySet = true;
    LOGFLF(LogLevel::info, "capture geometry [", id, "]:", info.regionX, ",",
           info.regionY, " ", info.regionW, "x", info.regionH,
           " cursor:", info.cursorMode);
  }
}

// ========== 音频腿回调(WASAPI 线程) ==========

void CaptureSession::AudioLeg::onAudioDesc(const AudioDesc& d) {
  if (!session) {
    return;
  }
  desc = d;
  // WavSave 头在 open 时占位(崩溃也是可播文件), 平面格式自动转 interleaved
  writer.reset(createWavSave());
  path = session->outputDir + "/audio/" + id + ".wav";
  if (!writer->setAudioDesc(d) || !writer->openUrl(path.c_str())) {
    session->notifyError(id.c_str(), AVError::dataNoVaild,
                         "wav track open failed");
    writer = nullptr;
    return;
  }
  lastPatchTick = timeTick();
  LOGFLF(LogLevel::info, "capture audio [", id, "]:", d.sampleRate, "Hz",
         d.channels, "ch fmt:", (int32_t)d.format);
}

void CaptureSession::AudioLeg::onAudioError(AVError err, const char* msg) {
  if (session) {
    session->notifyError(id.c_str(), err, msg);
  }
}

void CaptureSession::AudioLeg::onAudioFrame(const AvoxAFrame& frame) {
  CaptureSession* s = session;
  if (!s || !s->bRecording() || !frame.buffer.data || frame.buffer.size <= 0) {
    return;
  }
  if (startMs < 0) {
    startMs = s->activeMs();
  }
  if (!writer || !writer->bOpen()) {
    // WAV 头未就绪(onAudioDesc 未到): 丢弃并计数, 不阻塞采集
    droppedBytes += frame.buffer.size;
    return;
  }
  writer->addFrame(frame.buffer);
  // 头长度周期回填(崩溃恢复)
  int64_t now = timeTick();
  if (now - lastPatchTick > kWavPatchIntervalTick) {
    writer->patchHeader();
    lastPatchTick = now;
  }
}

void CaptureSession::AudioLeg::onAudioClose() {}

// ========== 文件清理与 session.json ==========

void CaptureSession::cleanupEmptyFiles() {
  std::error_code ec;
  for (auto& leg : videos) {
    // 零帧视频文件只剩容器头(首帧超时/打不开的现场), 删除不留给 finalize
    if (leg->frames.load() == 0 && !leg->path.empty()) {
      std::filesystem::remove(leg->path, ec);
      LOGFLF(LogLevel::warn, "removed empty video file:", leg->path);
    }
  }
}

void CaptureSession::removeDraft() {
  std::error_code ec;
  std::filesystem::remove(outputDir + "/session.draft.json", ec);
}

bool CaptureSession::writeSessionInfo(const char* dir, bool bDraft) {
  if (!dir) {
    return false;
  }
  std::string path = std::string(dir) + (bDraft ? "/session.draft.json"
                                                : "/session.json");
  // UTF-8 路径自愈(中文输出目录): Windows 走 _wfopen, 其余平台 fopen
  std::FILE* f = openFileUtf8(path.c_str(), "wb");
  if (!f) {
    LOGFLF(LogLevel::error, "session info open failed:", path);
    return false;
  }
  CaptureGeometry geo = {};
  getCaptureGeometry(&geo);
  int64_t videoPts0Ms = firstFrameMs.load() > 0 ? firstFrameMs.load() : 0;
  std::fprintf(f, "{\n");
  std::fprintf(f, "  \"version\": 1,\n");
  std::fprintf(f, "  \"draft\": %s,\n", bDraft ? "true" : "false");
  std::fprintf(f, "  \"clock\": { \"t0WallMs\": %lld, \"videoPts0Ms\": %lld },\n",
               (long long)(t0Tick / 10000), (long long)videoPts0Ms);
  std::fprintf(f,
               "  \"geometry\": { \"x\": %d, \"y\": %d, \"width\": %d, "
               "\"height\": %d },\n",
               geo.x, geo.y, geo.width, geo.height);
  std::fprintf(f, "  \"cursorMode\": \"%s\",\n", escapeJson(cursorMode).c_str());
  std::fprintf(f, "  \"videos\": [");
  for (size_t i = 0; i < videos.size(); i++) {
    auto& leg = videos[i];
    int64_t dropped = 0;
    if (leg->muxer) {
      dropped = leg->muxer->getDroppedBySize() +
                leg->muxer->getDroppedByFpsGate() +
                leg->muxer->getEncoderDropped();
    }
    std::fprintf(f,
                 "%s\n    { \"id\": \"%s\", \"path\": \"%s\", \"width\": %d, "
                 "\"height\": %d, \"fps\": %.3f, \"frames\": %lld, "
                 "\"dropped\": %lld }",
                 i ? "," : "", escapeJson(leg->id).c_str(),
                 escapeJson(leg->path).c_str(), leg->desc.width,
                 leg->desc.height, leg->desc.fps,
                 (long long)leg->frames.load(), (long long)dropped);
  }
  std::fprintf(f, "\n  ],\n");
  std::fprintf(f, "  \"audios\": [");
  for (size_t i = 0; i < audios.size(); i++) {
    auto& leg = audios[i];
    std::fprintf(f,
                 "%s\n    { \"id\": \"%s\", \"path\": \"%s\", "
                 "\"sampleRate\": %d, \"channels\": %d, \"format\": \"%s\", "
                 "\"startMs\": %lld, \"bytes\": %lld }",
                 i ? "," : "", escapeJson(leg->id).c_str(),
                 escapeJson(leg->path).c_str(), leg->desc.sampleRate,
                 (int32_t)leg->desc.channels,
                 getAudioFormatStr(leg->desc.format), (long long)leg->startMs,
                 (long long)(leg->writer ? leg->writer->getDataBytes() : 0));
  }
  std::fprintf(f, "\n  ]\n}\n");
  std::fclose(f);
  LOGFLF(LogLevel::info, "session info written:", path);
  return true;
}

// ========== option 长尾键 ==========

void CaptureSession::onOptionChange(const char* key, ArgType option) {
  (void)option;
  if (equalsIgnoreCase(key, AVOX_REC_ENCODE_GOP_INT)) {
    cfg.videoEncode.gopSeconds = (int32_t)getInt(key);
  } else if (equalsIgnoreCase(key, AVOX_REC_ENCODE_PROFILE_INT)) {
    cfg.videoEncode.profile = (EncodeProfile)getInt(key);
  } else if (equalsIgnoreCase(key, AVOX_REC_HARD_ENCODE_BOOL)) {
    bHardEncode = getBool(key);
  } else {
    return;
  }
  LOGFLF(LogLevel::info, "capture option:", key, " change");
}

}
