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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
  yuvOut,           // 无vulkan直取: enableYuvOut 帧类型契约 (硬解 nv12 / 软解解码格式)
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
  // yuvOut 用例: 期望的首帧类型 (getYuvTypeStr 口径), 空 = 不判型
  std::string expectType;
  // false = 默认不跑 (已知未修/环境依赖), 需 --all 显式打开
  bool enabled = true;
};

// ── 用例表: 26 条, 轴 + 固定交叉 (不做全笛卡尔) ──
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
    // 截图走平台原生渲染 (关 vulkan): 离屏 vulkan 路线 fetchFrame 返回 0, 见 README 已知取舍。
    // 用本地文件而不是网络源: 截图能力与协议无关, 这样它也能进离线子集(CI 覆盖)
    PlayCase c;
    c.id = "shot";
    c.kind = CaseKind::screenShot;
    c.url = ep.fileH264;
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
  // F 无vulkan直取 (车道B): 关 vulkan 走平台原生渲染 + enableYuvOut, 严格判交付帧类型。
  // 硬解应交付解码直出 nv12 (DX11 staging / Metal readback), 类型不对 = 车道断裂或
  // 硬解回退软解, 都判 FAIL —— 这是"硬解组件被裁/回退"的哨兵 (ffmpeg9 裁 hwaccel 一类
  // 回归只有它能抓到)。真硬解需 GPU 视频单元, CI 离线子集要排除 (OFFLINE_SKIP)
  {
    PlayCase c;
    c.id = "yuvout-h264";
    c.kind = CaseKind::yuvOut;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.nativeRender = true;
    c.expectType = "nv12";
    cases.push_back(c);
  }
  {
    // 软解 cpuIn: 基类零拷 packed 视图, 交付解码原始格式 (源 webrtc_pull.mp4 为 yuv420p)
    PlayCase c;
    c.id = "yuvout-h264-soft";
    c.kind = CaseKind::yuvOut;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.hardDecode = false;
    c.nativeRender = true;
    c.expectType = "yuv420P";
    cases.push_back(c);
  }
  {
    // 无vulkan转码录制: SurfaceRenderNative::pushFrame 非 vk 分支 (getCpuFrame→muxer),
    // 该分支在车道 B 之前完全不存在 (没 vulkan 转码录制拿不到帧)。
    // 注: copy 直通的包走 IO 层 onPacket, 不经渲染层, 测不到本分支, 故用转码
    PlayCase c;
    c.id = "rec-transcode-novk";
    c.kind = CaseKind::recordTranscode;
    c.url = ep.fileH264;
    c.seconds = 8;
    c.nativeRender = true;
    cases.push_back(c);
  }
  // AVOX_PM_SOFT=1 (Apple 宿主启动参数 --soft): 全部用例强制软解。
  // 逃生门: 真机硬解服务被系统状态楔死时 (iOS 26 实测 VTDecompressionSessionCreate
  // 挂死不返回), 仍能跑完整矩阵验证 app/判定/日志链路。yuvout-h264 会因
  // 交付类型变 yuv420P 判 FAIL 属本模式预期 (哨兵语义不改)。
  if (std::getenv("AVOX_PM_SOFT") != nullptr) {
    for (auto &c : cases) {
      c.hardDecode = false;
    }
  }
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

// ── 图像的客观统计 (不看内容, 只排除"废图") ──
// 渲染链路的回归里绝大多数坏图是: 纯色/黑屏/白屏/绿屏/静止不动 —— 这些几行算术就能
// 确定性判定, 比调大模型快几个数量级且可复现。需要语义判断的(人脸像不像/水印干不干净)
// 才轮到模型, 见 README「画面质量怎么自动判」。
struct ImageStats {
  double meanLuma = 0;
  double stdLuma = 0;       // 灰度标准差: 纯色/纯屏时接近 0
  double topColorRatio = 0; // 最常见颜色的占比: 纯色/卡帧时接近 1
  int32_t width = 0;
  int32_t height = 0;
};

// 抽样统计 (最多 128x128 个点), 大图也不拖慢
inline ImageStats analyzeImage(IImageBuffer* buf) {
  ImageStats st;
  if (!buf || !buf->getPointer()) {
    return st;
  }
  ImageFormat fmt = buf->getImageFormat();
  if (fmt.width <= 0 || fmt.height <= 0) {
    return st;
  }
  st.width = fmt.width;
  st.height = fmt.height;
  int32_t px = getPixelSize(fmt.imageType);
  if (px < 3) {  // 至少 3 通道才谈得上色偏判断
    return st;
  }
  const uint8_t* base = buf->getPointer();
  int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * px;
  int32_t stepX = fmt.width > 128 ? fmt.width / 128 : 1;
  int32_t stepY = fmt.height > 128 ? fmt.height / 128 : 1;
  std::map<uint32_t, int32_t> hist;  // 每通道 5bit 量化后的桶
  double sum = 0;
  double sum2 = 0;
  int64_t n = 0;
  for (int32_t y = 0; y < fmt.height; y += stepY) {
    const uint8_t* row = base + (size_t)y * pitch;
    for (int32_t x = 0; x < fmt.width; x += stepX) {
      const uint8_t* p = row + (size_t)x * px;
      // 通道序随原生格式(bgra/rgba)不同, 求亮度时权重会略有偏差;
      // 但黑屏/白屏/纯色的判定不受影响, 只用于粗筛
      double luma = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
      sum += luma;
      sum2 += luma * luma;
      ++n;
      uint32_t key = ((uint32_t)(p[0] >> 3) << 10) | ((uint32_t)(p[1] >> 3) << 5) |
                     (uint32_t)(p[2] >> 3);
      ++hist[key];
    }
  }
  if (n == 0) {
    return st;
  }
  st.meanLuma = sum / (double)n;
  st.stdLuma = std::sqrt(std::max(0.0, sum2 / (double)n - st.meanLuma * st.meanLuma));
  int32_t top = 0;
  for (const auto& kv : hist) {
    top = std::max(top, kv.second);
  }
  st.topColorRatio = (double)top / (double)n;
  return st;
}

// 判"这张图不是废图": 有细节、亮度不贴边、不是一整块纯色
inline bool imageLooksAlive(const ImageStats& st, std::string& why) {
  if (st.width <= 0 || st.height <= 0) {
    why = "no-image";
    return false;
  }
  if (st.meanLuma < 12.0 || st.meanLuma > 243.0) {
    why = "near-black-or-white";
    return false;
  }
  if (st.stdLuma < 6.0) {
    why = "flat-no-detail";
    return false;
  }
  if (st.topColorRatio > 0.95) {
    why = "single-color";
    return false;
  }
  return true;
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
  std::string firstType;

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
      firstType = getYuvTypeStr(yuvType);
      firstFmt = firstType + "-" + std::to_string(yf.width) +
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
  std::string firstTypeStr() {
    std::lock_guard<std::mutex> lock(mtx);
    return firstType;
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

// ── 无vulkan直取 (车道B): enableYuvOut 帧类型契约 ──
// 判: 帧数 + packed 契约 + 首帧类型 = expectType; 类型不对即车道断裂/硬解回退
inline Attempt yuvOutAttempt(const PlayCase& c, const std::string& prefix) {
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
  std::string type = fob.firstTypeStr();
  if (ok && !c.expectType.empty() && type != c.expectType) {
    ok = false;
  }
  char buf[320];
  std::snprintf(buf, sizeof(buf), "frames=%d dumped=%d type=%s expect=%s", fob.count(),
                fob.dumpedCount(), type.c_str(), c.expectType.c_str());
  if (!ok) {
    if (!fob.ok()) {
      r.note = fob.why() + " ";
    } else if (fob.count() < 10) {
      r.note = "frames-too-few(" + std::to_string(fob.count()) + ") ";
    } else {
      r.note = "type-mismatch ";
    }
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
  // 先统计再存盘/释放: 像素只在解码输出缓冲上读一遍
  ImageStats st = grabbed ? analyzeImage(shot) : ImageStats{};
  bool saved = grabbed && saveImagePath(path.c_str(), shot);
  delete shot;
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  int64_t bytes = saved ? fileSize(path) : -1;
  // 拿不到像素格式(图仍可能存盘成功)时不做质量判定, 避免误杀; note 里会标 stats=na
  bool haveStats = st.width > 0 && st.height > 0;
  std::string why;
  bool alive = !haveStats || imageLooksAlive(st, why);
  r.pass = playing && saved && bytes > 0 && alive;
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "playing=%d grab=%d bytes=%lld luma=%.1f std=%.1f top=%.2f %dx%d out=%s",
                (int)playing, (int)grabbed, (long long)bytes, st.meanLuma, st.stdLuma,
                st.topColorRatio, st.width, st.height, path.c_str());
  if (!r.pass) {
    r.note = grabbed ? ("image-" + why + " ") : "shot-failed ";
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
  // 录制取帧走离屏路径, 与 yuvouttest 同口径; novk 用例关 vulkan 走 pushFrame 非vk分支
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
  // Windows 上 close() 返回后句柄可能仍被写线程短暂持有, 立刻 fopen 会因共享冲突失败
  // (实测 flake: bytes=-1 而文件其实存在) —— 退避重试最多 2s
  int64_t bytes = -1;
  for (int32_t i = 0; i < 20; ++i) {
    bytes = fileSize(outPath);
    if (bytes > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
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
      case CaseKind::yuvOut:
        r = yuvOutAttempt(c, joinPath(opt.outDir, opt.prefix));
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
  static const char* kKinds[] = {"pull",     "rtc",      "frame",   "shot",
                                 "rec-copy", "rec-trans", "yuv-out"};
  for (const PlayCase& c : cases) {
    const char* kind = kKinds[(int)c.kind];
    std::printf("%-20s %-10s io=%-11s dec=%-4s %2ds %s %s\n", c.id.c_str(), kind,
                getIoPlanStr(c.io), c.hardDecode ? "hard" : "soft", c.seconds,
                c.enabled ? "   " : "(off)", c.url.c_str());
  }
}

}  // namespace playmatrix
}  // namespace avox
