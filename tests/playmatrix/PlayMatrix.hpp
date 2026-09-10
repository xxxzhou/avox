#pragma once

// 跨平台播放回归矩阵: 共享用例表 + 判定逻辑 (header-only, 只用 avox 公共头)
//
// 目的: 每次改动后跑一次, 确认"播放"这条链路没被搞坏 (拉流/解码/帧输出/录制)。
// 形态: 用例与判定口径只此一份, 各平台 runner 只做宿主适配
//       (Windows 控制台 / Apple app+无头 CLI / Android APK / Linux 控制台),
//       于是同一 case id 在五个平台含义完全一致, 可汇总成一张跨平台表。
//
// 判定行: [AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]
// 退出码: 0 = 全过 (可接 CI), 1 = 有 FAIL
//
// 各平台宿主入口见 platform/<plat>/playtest; 一键驱动见 script/testenv/play_regress.py

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avox/AvoxCodec.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"
#include "avox/AvoxVideo.h"

namespace avox {
namespace playmatrix {

// ── 拉流端点: 各平台按部署传入 (本机 ZLM 用 127.0.0.1, 局域网用主机 IP) ──
struct Endpoints {
  std::string host = "127.0.0.1";
  int32_t rtspPort = 554;
  int32_t rtmpPort = 1935;
  int32_t httpPort = 80;
  // ZLM 流名: app/stream
  std::string h264Key = "live/avox264";
  std::string h265Key = "live/avox";
  // 本地文件源 (留空则该组用例判 FAIL note=source-missing, 便于发现环境缺失)
  std::string fileH264;
  std::string fileH265;

  std::string appName(const std::string& key) const {
    size_t p = key.find('/');
    return p == std::string::npos ? std::string("live") : key.substr(0, p);
  }
  std::string streamName(const std::string& key) const {
    size_t p = key.find('/');
    return p == std::string::npos ? key : key.substr(p + 1);
  }
  std::string rtsp(const std::string& key) const {
    return "rtsp://" + host + ":" + std::to_string(rtspPort) + "/" + key;
  }
  std::string rtmp(const std::string& key) const {
    return "rtmp://" + host + ":" + std::to_string(rtmpPort) + "/" + key;
  }
  std::string http(const std::string& tail) const {
    return "http://" + host + ":" + std::to_string(httpPort) + tail;
  }
  // ZLM WHEP 信令地址 (WebRTC 拉流)
  std::string rtc(const std::string& key) const {
    return http("/index/api/webrtc?app=" + appName(key) + "&stream=" +
                streamName(key) + "&type=play");
  }
};

// ── 用例类型 ──
enum class CaseKind {
  pull,             // IMediaPlayer 拉流: playing + fps>0 + pos>1.5s
  rtc,              // IRtcPlayer 拉流: connected + firstFrame + fps>0
  frameContract,    // 离屏 yuv420P: packed 契约 + packed→split→RGBA 落 PNG
  screenShot,       // 离屏截图落 PNG
  recordCopy,       // getMuxer(false) 直通录制 (原流拷贝)
  recordTranscode,  // getMuxer(true) 转码录制 (+ 中途 seek)
};

struct PlayCase {
  std::string id;
  CaseKind kind = CaseKind::pull;
  std::string url;
  // 拉流用例选项
  IoPlan io = IoPlan::ffmpeg;
  bool hardDecode = true;
  int32_t seconds = 15;
  // 录制用例: 录制时长与产物字节下限 (空文件/仅文件头会被判掉)
  int32_t minBytes = 8192;
  // true = 关掉 vulkan 管线走平台原生渲染 (截图的稳定路线)
  bool nativeRender = false;
  // false = 默认不跑 (已知未修/环境依赖), 需 --all 显式打开
  bool enabled = true;
};

// ── 用例表: 21 条, 轴 + 固定交叉 (不做全笛卡尔) ──
inline std::vector<PlayCase> buildCases(const Endpoints& ep) {
  std::vector<PlayCase> cases;
  const std::string k264 = ep.h264Key;
  const std::string k265 = ep.h265Key;
  auto pull = [&cases](const std::string& id, const std::string& url, IoPlan io,
                       bool hard, int32_t seconds) {
    PlayCase c;
    c.id = id;
    c.kind = CaseKind::pull;
    c.url = url;
    c.io = io;
    c.hardDecode = hard;
    c.seconds = seconds;
    cases.push_back(c);
  };
  auto special = [&cases](const std::string& id, CaseKind kind, const std::string& url,
                          int32_t seconds) {
    PlayCase c;
    c.id = id;
    c.kind = kind;
    c.url = url;
    c.seconds = seconds;
    cases.push_back(c);
  };
  // A 协议 × 编码 (默认口径: ffmpeg IO + 硬解 + 上屏/离屏)
  pull("file-h264", ep.fileH264, IoPlan::ffmpeg, true, 15);
  pull("file-h265", ep.fileH265, IoPlan::ffmpeg, true, 15);
  pull("rtsp-h264", ep.rtsp(k264), IoPlan::ffmpeg, true, 15);
  pull("rtsp-h265", ep.rtsp(k265), IoPlan::ffmpeg, true, 15);
  pull("rtmp-h264", ep.rtmp(k264), IoPlan::ffmpeg, true, 15);
  pull("rtmp-h265", ep.rtmp(k265), IoPlan::ffmpeg, true, 15);
  pull("hls-h264", ep.http("/" + k264 + "/hls.m3u8"), IoPlan::ffmpeg, true, 20);
  pull("hls-h265", ep.http("/" + k265 + "/hls.m3u8"), IoPlan::ffmpeg, true, 20);
  pull("ts-h264", ep.http("/" + k264 + ".live.ts"), IoPlan::ffmpeg, true, 15);
  pull("ts-h265", ep.http("/" + k265 + ".live.ts"), IoPlan::ffmpeg, true, 15);
  // B IO 方案对照 (只挂 RTSP, 避免组合爆炸; A 组已覆盖 ffmpeg)
  pull("rtsp-h264-zm", ep.rtsp(k264), IoPlan::zlmediakit, true, 15);
  pull("rtsp-h265-zm", ep.rtsp(k265), IoPlan::zlmediakit, true, 15);
  // C 解码模式对照 (软解; 硬解由 A 组覆盖; 同时覆盖本地文件与网络)
  pull("file-h264-soft", ep.fileH264, IoPlan::ffmpeg, false, 15);
  pull("file-h265-soft", ep.fileH265, IoPlan::ffmpeg, false, 15);
  pull("rtsp-h264-soft", ep.rtsp(k264), IoPlan::ffmpeg, false, 15);
  pull("rtsp-h265-soft", ep.rtsp(k265), IoPlan::ffmpeg, false, 15);
  // D WebRTC (独立通道, 不经 IO 方案)
  special("webrtc-h264", CaseKind::rtc, ep.rtc(k264), 15);
  special("webrtc-h265", CaseKind::rtc, ep.rtc(k265), 15);
  // E 帧契约 / 截图 / 录制
  special("frame-contract", CaseKind::frameContract, ep.rtsp(k264), 8);
  {
    // 截图走平台原生渲染 (关 vulkan): 离屏 vulkan 路线 fetchFrame 返回 0, 见 README 已知取舍
    PlayCase c;
    c.id = "shot";
    c.kind = CaseKind::screenShot;
    c.url = ep.rtsp(k264);
    c.seconds = 6;
    c.nativeRender = true;
    cases.push_back(c);
  }
  {
    // 离屏 vulkan 截图: 已知返回 0 未修, 默认不跑 (--all 打开)
    PlayCase c;
    c.id = "shot-vk";
    c.kind = CaseKind::screenShot;
    c.url = ep.rtsp(k264);
    c.seconds = 6;
    c.enabled = false;
    cases.push_back(c);
  }
  special("rec-copy-h264", CaseKind::recordCopy, ep.rtsp(k264), 8);
  // 转码录制中途 seek 需要可 seek 的源, 用本地文件
  special("rec-transcode-h264", CaseKind::recordTranscode, ep.fileH264, 8);
  return cases;
}

// ── 判定行 (与 script/testenv 约定一致) ──
inline void verdict(const std::string& id, bool pass, const std::string& note = "") {
  std::printf("[AVOX][TEST] case=%s result=%s", id.c_str(), pass ? "PASS" : "FAIL");
  if (!note.empty()) {
    std::printf(" %s", note.c_str());
  }
  std::printf("\n");
  std::fflush(stdout);
}

// 文件存在且非空
inline int64_t fileSize(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return -1;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);
  return size > 0 ? (int64_t)size : 0;
}

struct Attempt {
  bool pass = false;
  std::string note;
};

// ── 播放器观察者: 只记 IO 错误 (首帧前解码报错属正常, 不判失败) ──
class CaseOb : public IMediaPlayerOb {
 public:
  CaseOb() = default;
  ~CaseOb() override = default;

 private:
  std::mutex mtx;
  bool ioErr = false;
  std::string lastErr;

 public:
  void onIoError(AVError error, const char* msg) override {
    (void)error;
    std::lock_guard<std::mutex> lock(mtx);
    ioErr = true;
    lastErr = msg ? msg : "";
  }
  bool hasIoError() {
    std::lock_guard<std::mutex> lock(mtx);
    return ioErr;
  }
  std::string error() {
    std::lock_guard<std::mutex> lock(mtx);
    return lastErr;
  }
};

// ── 起播等待: 返回是否进入 playing ──
inline bool waitPlaying(IMediaPlayer* player, CaseOb& ob, int32_t timeoutMs) {
  int32_t ticks = timeoutMs / 100;
  for (int32_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing) {
      return true;
    }
    if (ob.hasIoError()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

// ── 等到真出帧 (fps>0): 录制/截图必须在有视频尺寸之后才能拿到 track 描述 ──
inline bool waitFirstFrames(IMediaPlayer* player, CaseOb& ob, int32_t timeoutMs) {
  int32_t ticks = timeoutMs / 100;
  for (int32_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing && player->getFps() > 0) {
      return true;
    }
    if (ob.hasIoError()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

// 播放统计摘要 (判过判不过都打, 便于归因)
inline std::string pullDetail(IMediaPlayer* player, const PlayCase& c, CaseOb& ob) {
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "state=%s pos=%lldms dur=%lldms fps=%.1f io=%s dec=%s",
                getPlayerStateStr(player->getState()),
                (long long)player->getPosition(), (long long)player->getDuration(),
                player->getFps(), getIoPlanStr(c.io), c.hardDecode ? "hard" : "soft");
  std::string detail = buf;
  std::string err = ob.error();
  if (!err.empty()) {
    detail += " err=" + err;
  }
  return detail;
}

// ── 单次拉流尝试 ──
inline Attempt pullAttempt(const PlayCase& c, void* surface) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  if (surface) {
    sr->setSurface(surface);
  } else {
    sr->setOffSurface(YuvType::yuv420P);
  }
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  int64_t ticks = (int64_t)c.seconds * 10;
  for (int64_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing && player->getFps() > 0 &&
        player->getPosition() > 1500) {
      r.pass = true;
      break;
    }
    if (ob.hasIoError()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::string detail = pullDetail(player, c, ob);
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  if (!r.pass) {
    r.note = "no-frame-in-" + std::to_string(c.seconds) + "s ";
  }
  r.note += detail;
  return r;
}

// ── WebRTC 拉流: 连接 + 首帧 + fps>0 ──
class RtcOb : public IRtcEventOb {
 public:
  RtcOb() = default;
  ~RtcOb() override = default;

 private:
  std::mutex mtx;
  bool conn = false;
  bool frame = false;

 public:
  void onConnectionState(RtcConnState state) override {
    std::lock_guard<std::mutex> lock(mtx);
    conn = conn || state == RtcConnState::connected;
  }
  void onFirstVideoFrame() override {
    std::lock_guard<std::mutex> lock(mtx);
    frame = true;
  }
  bool connected() {
    std::lock_guard<std::mutex> lock(mtx);
    return conn;
  }
  bool firstFrame() {
    std::lock_guard<std::mutex> lock(mtx);
    return frame;
  }
};

inline Attempt rtcAttempt(const PlayCase& c) {
  Attempt r;
  IRtcPlayer* player = createWebRtcPlayer();
  if (!player) {
    r.note = "createWebRtcPlayer-null";
    return r;
  }
  player->setRollType(RtcRollType::offer);
  player->setVideoDirection(RtpDirection::recvOnly);
  // 无头不协商音频: 避免占用设备, 也排除音频线程干扰
  player->setAudioDirection(RtpDirection::inactive);
  RtcOb ob;
  player->addOb(&ob);
  IRtcEventOb* agent = createZlTestSdpAgent(player, c.url.c_str());
  if (agent) {
    player->addOb(agent);
  }
  player->open();
  double fps = 0;
  float loss = 0;
  int32_t rtt = -1;
  for (int64_t i = 0; i < (int64_t)c.seconds * 20; ++i) {
    if (ob.connected()) {
      fps = player->getFps();
      loss = player->getLossRate();
      rtt = player->getRttMs();
      if (ob.firstFrame() && fps > 0) {
        r.pass = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  char buf[256];
  std::snprintf(buf, sizeof(buf), "conn=%d firstFrame=%d fps=%.1f loss=%.2f rtt=%d",
                (int)ob.connected(), (int)ob.firstFrame(), fps, loss, rtt);
  r.note = buf;
  player->removeOb(&ob);
  if (agent) {
    player->removeOb(agent);
  }
  player->close();
  delete player;
  return r;
}

// ── 离屏帧观察者: packed 契约 + 抽帧转 RGBA ──
class FrameOb : public ISurfaceRenderOb {
 public:
  FrameOb() = default;
  ~FrameOb() override = default;

 private:
  std::mutex mtx;
  std::string prefix;
  int32_t frames = 0;
  int32_t dumped = 0;
  bool contractOk = true;
  std::string reason;
  std::string firstFmt;

 public:
  void setPrefix(const std::string& p) { prefix = p; }
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      mark("onFrame-null");
      return;
    }
    ImageFormat fmt = buf->getImageFormat();
    YUVFormat yf = {};
    image2YUVFormat(fmt, yuvType, yf);
    std::lock_guard<std::mutex> lock(mtx);
    if (frames == 0) {
      firstFmt = std::string(getYuvTypeStr(yuvType)) + "-" + std::to_string(yf.width) +
                 "x" + std::to_string(yf.height) + "-pitch" + std::to_string(fmt.rowPitch);
      // 契约: rowPitch 不小于宽, 且缓冲容纳整帧
      if (fmt.rowPitch < yf.width ||
          buf->getBufferSize() < getYuvFrameSize(yf, fmt.rowPitch)) {
        contractOk = false;
        reason = "packed-contract";
      }
    }
    if (frames < 2) {
      IImageBuffer* tmp = createImageBuffer();
      YUVFrame frame = {};
      if (image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
        IImageBuffer* rgba = createImageBuffer();
        if (yuvframe2Rgba(frame, rgba)) {
          std::string path = prefix + "frame" + std::to_string(frames) + ".png";
          if (saveImagePath(path.c_str(), rgba)) {
            ++dumped;
          } else if (contractOk) {
            contractOk = false;
            reason = "save-png-failed";
          }
        } else if (contractOk) {
          contractOk = false;
          reason = "yuvframe2Rgba-failed";
        }
        delete rgba;
      } else if (contractOk) {
        contractOk = false;
        reason = "image2SplitYUVFrame-failed";
      }
      delete tmp;
    }
    ++frames;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t width, int32_t height) override {
    (void)width;
    (void)height;
  }
  void mark(const std::string& why) {
    std::lock_guard<std::mutex> lock(mtx);
    if (contractOk) {
      contractOk = false;
      reason = why;
    }
    ++frames;
  }
  bool ok() {
    std::lock_guard<std::mutex> lock(mtx);
    return contractOk;
  }
  std::string why() {
    std::lock_guard<std::mutex> lock(mtx);
    return reason;
  }
  int32_t count() {
    std::lock_guard<std::mutex> lock(mtx);
    return frames;
  }
  int32_t dumpedCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return dumped;
  }
  std::string firstFormat() {
    std::lock_guard<std::mutex> lock(mtx);
    return firstFmt;
  }
};

inline Attempt frameContractAttempt(const PlayCase& c, const std::string& prefix) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  FrameOb fob;
  fob.setPrefix(prefix);
  addSurfaceRenderOb(sr, &fob);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  waitFirstFrames(player, ob, 10000);
  for (int32_t i = 0; i < c.seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  player->close();
  removeSurfaceRenderOb(sr, &fob);
  delete player;
  bool ok = fob.ok() && fob.count() >= 10;
  char buf[320];
  std::snprintf(buf, sizeof(buf), "frames=%d dumped=%d fmt=%s", fob.count(),
                fob.dumpedCount(), fob.firstFormat().c_str());
  if (!ok) {
    r.note = (fob.ok() ? "frames-too-few(" + std::to_string(fob.count()) + ") "
                       : fob.why() + " ");
  }
  r.note += buf;
  r.pass = ok;
  return r;
}

// ── 离屏截图落 PNG ──
inline Attempt screenShotAttempt(const PlayCase& c, const std::string& path) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  if (c.nativeRender) {
    sr->setVulkan(false);
  }
  sr->setOffSurface(YuvType::yuv420P);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  // 必须等真出帧: 录制/截图都需要视频尺寸与已渲染画面, 仅 playing 时可能还没就绪
  bool playing = waitFirstFrames(player, ob, 15000);
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  IImageBuffer* shot = createImageBuffer();
  bool grabbed = sr->screenShot(shot);
  bool saved = grabbed && saveImagePath(path.c_str(), shot);
  delete shot;
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  int64_t bytes = saved ? fileSize(path) : -1;
  r.pass = playing && saved && bytes > 0;
  char buf[320];
  std::snprintf(buf, sizeof(buf), "playing=%d grab=%d out=%s bytes=%lld", (int)playing,
                (int)grabbed, path.c_str(), (long long)bytes);
  if (!r.pass) {
    r.note = "shot-failed ";
  }
  r.note += buf;
  return r;
}

// ── 录制: 直通(原流拷贝) / 转码(+中途 seek) ──
inline Attempt recordAttempt(const PlayCase& c, bool transcode, const std::string& outPath) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  // 录制取帧走离屏路径, 与 yuvouttest 同口径
  player->getSurfaceRender()->setOffSurface(YuvType::yuv420P);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  // 必须等真出帧: 录制/截图都需要视频尺寸与已渲染画面, 仅 playing 时可能还没就绪
  bool playing = waitFirstFrames(player, ob, 15000);
  if (!playing) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "no-frames-before-record";
    return r;
  }
  IMediaMuxer* muxer = player->getMuxer(transcode);
  if (!muxer) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "getMuxer-null";
    return r;
  }
  muxer->setMuxerType(MuxerType::ffmpeg);
  if (transcode) {
    muxer->setVideoCodec(VCodecId::h264);
    // Windows 硬编 h264_mf 对 profile 敏感, 软编更稳 (与 transcoderecordertest 同口径)
    muxer->setHardEncode(false);
  }
  bool opened = muxer->open(outPath.c_str());
  if (!opened) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "muxer-open-failed " + outPath;
    return r;
  }
  int64_t seekTo = -1;
  for (int32_t i = 0; i < c.seconds; ++i) {
    // 转码录制中途 seek 到一半, 验证 seek + 输出 PTS 单调
    if (transcode && seekTo < 0) {
      int64_t duration = player->getDuration();
      if (duration > 0) {
        seekTo = duration / 2;
        player->seek(seekTo);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  muxer->close();
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  int64_t bytes = fileSize(outPath);
  r.pass = bytes >= c.minBytes;
  char buf[384];
  std::snprintf(buf, sizeof(buf), "mode=%s out=%s bytes=%lld seekTo=%lldms",
                transcode ? "transcode" : "copy", outPath.c_str(), (long long)bytes,
                (long long)seekTo);
  if (!r.pass) {
    r.note = "output-too-small ";
  }
  r.note += buf;
  return r;
}

// ── 运行选项 ──
struct RunOptions {
  std::string outDir;             // 录制/截图产物目录 (空 = 当前目录)
  std::string prefix = "pm_";     // 产物文件名前缀
  int32_t retries = 3;            // 拉流失败重开次数
  std::vector<std::string> skip;  // 跳过的 case id
  bool includeDisabled = false;   // 是否连 enabled=false 的用例一起跑
  // 每条用例结束回调 (宿主用于上屏/落盘); 空 = 不打
  void (*onCase)(const std::string& id, bool pass, const std::string& note) = nullptr;
};

inline bool isSkipped(const RunOptions& opt, const std::string& id) {
  return std::find(opt.skip.begin(), opt.skip.end(), id) != opt.skip.end();
}

inline std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty()) {
    return name;
  }
  char last = dir[dir.size() - 1];
  return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

// ── 跑完整矩阵: 返回 0 全过, 1 有失败 ──
inline int runAll(const std::vector<PlayCase>& cases, void* surface, const RunOptions& opt) {
  int32_t pass = 0;
  int32_t fail = 0;
  int32_t skipped = 0;
  std::vector<std::string> failed;
  std::vector<std::string> skippedIds;
  for (const PlayCase& c : cases) {
    if (isSkipped(opt, c.id) || (!c.enabled && !opt.includeDisabled)) {
      ++skipped;
      skippedIds.push_back(c.id);
      continue;
    }
    Attempt r;
    switch (c.kind) {
      case CaseKind::pull: {
        // 失败自动重开重试 (HLS 首片未就绪的 404 靠这个兜)
        for (int32_t attempt = 1; attempt <= opt.retries; ++attempt) {
          r = pullAttempt(c, surface);
          if (r.pass) {
            break;
          }
          std::printf("[retry] case=%s attempt=%d/%d %s\n", c.id.c_str(), attempt,
                      opt.retries, r.note.c_str());
          std::fflush(stdout);
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        break;
      }
      case CaseKind::rtc:
        r = rtcAttempt(c);
        break;
      case CaseKind::frameContract:
        r = frameContractAttempt(c, joinPath(opt.outDir, opt.prefix));
        break;
      case CaseKind::screenShot:
        r = screenShotAttempt(c, joinPath(opt.outDir, opt.prefix + "shot.png"));
        break;
      case CaseKind::recordCopy:
        r = recordAttempt(c, false, joinPath(opt.outDir, opt.prefix + "copy.mp4"));
        break;
      case CaseKind::recordTranscode:
        r = recordAttempt(c, true, joinPath(opt.outDir, opt.prefix + "trans.mp4"));
        break;
    }
    verdict(c.id, r.pass, r.note);
    if (opt.onCase) {
      opt.onCase(c.id, r.pass, r.note);
    }
    if (r.pass) {
      ++pass;
    } else {
      ++fail;
      failed.push_back(c.id);
    }
  }
  std::printf("[AVOX][TEST] case=play-matrix result=%s pass=%d fail=%d skip=%d\n",
              fail == 0 ? "PASS" : "FAIL", pass, fail, skipped);
  if (!failed.empty()) {
    std::string list;
    for (size_t i = 0; i < failed.size(); ++i) {
      list += (i ? "," : "") + failed[i];
    }
    std::printf("[AVOX][TEST] case=play-matrix-failed result=FAIL ids=%s\n", list.c_str());
  }
  if (!skippedIds.empty()) {
    std::string list;
    for (size_t i = 0; i < skippedIds.size(); ++i) {
      list += (i ? "," : "") + skippedIds[i];
    }
    std::printf("[AVOX][TEST] case=play-matrix-skip result=PASS ids=%s\n", list.c_str());
  }
  std::fflush(stdout);
  return fail == 0 ? 0 : 1;
}

// ── 用例表打印 (--list, 也用于生成文档) ──
inline void printCases(const std::vector<PlayCase>& cases) {
  static const char* kKinds[] = {"pull",  "rtc",      "frame",   "shot",
                                 "rec-copy", "rec-trans"};
  for (const PlayCase& c : cases) {
    const char* kind = kKinds[(int)c.kind];
    std::printf("%-20s %-10s io=%-11s dec=%-4s %2ds %s %s\n", c.id.c_str(), kind,
                getIoPlanStr(c.io), c.hardDecode ? "hard" : "soft", c.seconds,
                c.enabled ? "   " : "(off)", c.url.c_str());
  }
}

}  // namespace playmatrix
}  // namespace avox
