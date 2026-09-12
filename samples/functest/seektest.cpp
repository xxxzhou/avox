// seek 出图回归: 复现「起播后 seek 到指定位置, 画面是否正常」的用户场景。
// 动机: NAS(WebDAV http user:pass@ / SMB UNC / 本地文件)起播后拖到中段
//       (如 20 分钟), panvox 报画面不对 —— 本测试在引擎层隔离: 同样的 URL、
//       同样的硬解默认, seek 后解码器吐出的帧到底坏不坏。
// 车道: 离屏 yuv420P onFrame (yuvouttest 同款, 无头可跑, 抓的是解码真值;
//       不含 vulkan 合成层 —— 若本测试 PASS 而 app 里仍坏图, 嫌疑收敛到
//       合成/共享纹理那一层)。默认硬解 (产品配置), -soft 隔离软解。
// 判定: 三层 ——
//   1. 帧契约: seek 后帧数 >= 10 且 rowPitch/bufSize 容纳整帧
//   2. 客观像素 (同 playmatrix 三刀): luma 12~243 / std >= 6 / top <= 0.95
//   3. PNG 落盘 (pre/post 各若干张): 花屏/错帧这类"有细节的坏图"统计判不死,
//      留给人眼/VLM 复判, 判定行附 luma/std/top 数值对照基线
// 用法: seektest <url> [seekSec] [outprefix] [-hard|-soft]
//   seekSec 缺省 1200 (20 分钟); 传 0 = seek 到时长一半; URL 需自行百分号编码
// 判定: 末尾打印 [AVOX][TEST] case=seektest result=PASS|FAIL ...
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

namespace {

std::mutex g_mtx;
bool g_failed = false;
std::string g_reason;

void fail(const std::string& reason) {
  std::lock_guard<std::mutex> lock(g_mtx);
  if (!g_failed) {
    g_failed = true;
    g_reason = reason;
  }
  std::printf("[FAIL] %s\n", reason.c_str());
  std::fflush(stdout);
}

bool fileExists(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);
  return size > 0;
}

// ── 客观像素统计 (与 tests/playmatrix/PlayMatrix.hpp analyzeImage 同源) ──
struct ImageStats {
  double meanLuma = 0;
  double stdLuma = 0;
  double topColorRatio = 0;
  int32_t width = 0;
  int32_t height = 0;
};

ImageStats analyzeImage(IImageBuffer* buf) {
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
  if (px < 3) {
    return st;
  }
  const uint8_t* base = buf->getPointer();
  int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * px;
  int32_t stepX = fmt.width > 128 ? fmt.width / 128 : 1;
  int32_t stepY = fmt.height > 128 ? fmt.height / 128 : 1;
  std::map<uint32_t, int32_t> hist;
  double sum = 0;
  double sum2 = 0;
  int64_t n = 0;
  for (int32_t y = 0; y < fmt.height; y += stepY) {
    const uint8_t* row = base + (size_t)y * pitch;
    for (int32_t x = 0; x < fmt.width; x += stepX) {
      const uint8_t* p = row + (size_t)x * px;
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

const char* aliveWhy(const ImageStats& st) {
  if (st.width <= 0) {
    return "no-image";
  }
  if (st.meanLuma < 12.0) {
    return "near-black";
  }
  if (st.meanLuma > 243.0) {
    return "near-white";
  }
  if (st.stdLuma < 6.0) {
    return "flat-no-detail";
  }
  if (st.topColorRatio > 0.95) {
    return "single-color";
  }
  return "alive";
}

// ── 播放状态/错误信号 ──
struct StateOb : IMediaPlayerOb {
  std::mutex mtx;
  std::condition_variable cv;
  PlayerState state = PlayerState::none;
  bool ioErr = false;
  std::string errMsg;

  void onStateChange(PlayerState pre, PlayerState cur) override {
    std::lock_guard<std::mutex> lk(mtx);
    state = cur;
    cv.notify_all();
  }
  void onIoError(AVError error, const char* msg) override {
    std::lock_guard<std::mutex> lk(mtx);
    ioErr = true;
    errMsg = msg != nullptr ? msg : "";
    cv.notify_all();
  }
  bool waitState(PlayerState want, int timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return state == want || ioErr; }) &&
           state == want;
  }
};

// ── 帧观察: pre(基线)/post(seek后) 两阶段抽帧落盘 + 统计 ──
class SeekOb : public ISurfaceRenderOb {
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      fail("onFrame null buf");
      return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    ImageFormat fmt = buf->getImageFormat();
    YUVFormat yfmt = {};
    image2YUVFormat(fmt, yuvType, yfmt);
    if (fmt.rowPitch < fmt.width ||
        buf->getBufferSize() < getYuvFrameSize(yfmt, fmt.rowPitch)) {
      fail("packed 契约破坏: rowPitch<width 或 bufSize 不足");
      return;
    }
    if (preFrames == 0 && phase == 0) {
      std::printf("onFrame first: %dx%d (rowPitch %d) type %s bufSize %d\n",
                  yfmt.width, yfmt.height, fmt.rowPitch, getYuvTypeStr(yuvType),
                  buf->getBufferSize());
    }
    if (phase == 0) {
      // 基线: 只留前 2 张, 计数照跑 (起播帧流是否健康)
      if (preFrames < 2) {
        dump(buf, yuvType, "pre" + std::to_string(preFrames));
      }
      preFrames++;
      return;
    }
    // seek 后: 前 3 秒内每 ~12 帧抽一张 (避免相邻帧几乎相同), 最多 6 张
    postFrames++;
    if (postDumps < 6 && postFrames % 12 == 1) {
      std::string tag = "post" + std::to_string(postDumps);
      dump(buf, yuvType, tag);
      postDumps++;
    }
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t width, int32_t height) override {}

  void dump(IImageBuffer* buf, YuvType yuvType, const std::string& tag) {
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    if (!image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
      fail("image2SplitYUVFrame 失败 (" + tag + ")");
      delete tmp;
      return;
    }
    IImageBuffer* rgba = createImageBuffer();
    if (yuvframe2Rgba(frame, rgba)) {
      std::string path = prefix + tag + ".png";
      if (saveImagePath(path.c_str(), rgba)) {
        ImageStats st = analyzeImage(rgba);
        stats[tag] = st;
        std::printf("dump %s %dx%d luma=%.1f std=%.1f top=%.2f %s\n",
                    path.c_str(), st.width, st.height, st.meanLuma, st.stdLuma,
                    st.topColorRatio, aliveWhy(st));
        std::fflush(stdout);
      } else {
        fail("saveImagePath 失败 (" + tag + ")");
      }
    } else {
      fail("yuvframe2Rgba 失败 (" + tag + ")");
    }
    delete rgba;
    delete tmp;
  }

  std::mutex mtx;
  std::atomic<int> phase{0};  // 0=起播基线, 1=seek 后
  int64_t preFrames = 0;
  int64_t postFrames = 0;
  int postDumps = 0;
  std::string prefix = "seek_";
  std::map<std::string, ImageStats> stats;
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "";
  if (!url || !*url) {
    std::printf(
        "用法: seektest <url> [seekSec] [outprefix] [-hard|-soft] [-fflog]\n"
        "  seekSec 缺省 1200 (20 分钟); 传 0 = seek 到时长一半\n");
    return 2;
  }
  int seekSec = argc > 2 ? std::atoi(argv[2]) : 1200;
  std::string prefix = argc > 3 ? (std::string(argv[3]) + "_") : "seek_";
  bool hard = true;
  bool fflog = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-soft") {
      hard = false;
    } else if (arg == "-hard") {
      hard = true;
    } else if (arg == "-fflog") {
      fflog = true;
    }
  }

  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[AVOX][TEST] case=seektest result=FAIL reason=createMediaPlayer-null\n");
    return 1;
  }
  player->setHardDecode(hard);
  if (fflog) {
    // FFmpeg 日志放到 INFO: 看 mov demuxer 的 seek 落点 ("seeking to ...")。
    // 必须在 createMediaPlayer 之后调 —— 引擎初始化(regFFIO)会把级别设回
    // WARNING; avox 把 avutil 私有链接, 只能从同目录 dll 动态取函数
    HMODULE avutil = LoadLibraryA("avutil-61.dll");
    if (avutil) {
      auto setLevel = (void (*)(int))GetProcAddress(avutil, "av_log_set_level");
      if (setLevel) {
        setLevel(48);  // AV_LOG_INFO
        std::printf("ffmpeg log level -> INFO\n");
      }
    }
  }
  ISurfaceRender* sr = player->getSurfaceRender();
  // 离屏 yuv420P: 抓解码真值, 不经 vulkan 合成 (app 侧坏图若只出在合成层,
  // 本测试会 PASS —— 那正是分层定位要的结论)
  sr->setVulkan(false);
  sr->setOffSurface(YuvType::yuv420P);
  SeekOb ob;
  ob.prefix = prefix;
  addSurfaceRenderOb(sr, &ob);
  StateOb sob;
  addMediaPlayerOb(player, &sob);

  std::printf("mode: %s decode offscreen-yuv420P\nseektest open %s\n", hard ? "hard" : "soft",
              url);
  std::fflush(stdout);
  player->open(url);
  if (!sob.waitState(PlayerState::playing, 30000)) {
    fail("起播 30s 未到 playing (网络/凭据/容器?)");
  }
  int64_t dur = player->getDuration();
  std::printf("duration=%lldms\n", (long long)dur);

  // 基线稳态 2s: 帧流健康才谈得上 seek 后对照
  std::this_thread::sleep_for(std::chrono::seconds(2));
  {
    std::lock_guard<std::mutex> lock(ob.mtx);
    if (ob.preFrames < 5) {
      fail("起播基线帧数不足 (preFrames<5)");
    }
  }

  // seek 目标: 指定秒数; 超出时长则退回时长一半; 0 = 时长一半
  int64_t target = seekSec > 0 ? (int64_t)seekSec * 1000 : 0;
  if (dur > 0 && (target <= 0 || target >= dur - 5000)) {
    target = dur / 2;
  }
  if (target <= 0) {
    target = 1200 * 1000;  // 拿不到时长时的兜底
  }
  std::printf("seek -> %lldms\n", (long long)target);
  std::fflush(stdout);
  ob.phase = 1;  // onFrame 从此进入 post 采样
  player->seek(target);
  bool back = sob.waitState(PlayerState::playing, 20000);
  if (!back) {
    fail("seek 后 20s 未恢复 playing");
  }
  // 出画 + 解码追上渲染时钟, 让 post 采样攒够帧
  std::this_thread::sleep_for(std::chrono::seconds(4));

  player->close();
  removeMediaPlayerOb(player, &sob);
  removeSurfaceRenderOb(sr, &ob);
  delete player;

  // ── 判定 ──
  int64_t postFrames = ob.postFrames;
  int aliveOk = 0;
  int aliveBad = 0;
  std::string detail;
  for (const auto& [tag, st] : ob.stats) {
    bool isPost = tag.rfind("post", 0) == 0;
    if (!isPost) {
      continue;
    }
    const char* why = aliveWhy(st);
    bool ok = std::string(why) == "alive";
    ok ? aliveOk++ : aliveBad++;
    detail += tag + ":" + why + "(luma=" + std::to_string(st.meanLuma).substr(0, 5) +
              " std=" + std::to_string(st.stdLuma).substr(0, 5) + ") ";
    (void)ok;
  }
  bool pngsOk = true;
  for (int i = 0; i < 2; i++) {
    pngsOk = pngsOk && fileExists(prefix + "pre" + std::to_string(i) + ".png");
  }
  pngsOk = pngsOk && ob.postDumps > 0;
  for (int i = 0; i < ob.postDumps; i++) {
    pngsOk = pngsOk && fileExists(prefix + "post" + std::to_string(i) + ".png");
  }
  bool ok = !g_failed && back && ob.preFrames >= 5 && postFrames >= 10 && aliveBad == 0 && pngsOk;
  std::printf("[AVOX][TEST] case=seektest result=%s target=%lldms pre=%lld post=%lld "
              "postDumps=%d aliveOk=%d aliveBad=%d %s%s\n",
              ok ? "PASS" : "FAIL", (long long)target, (long long)ob.preFrames,
              (long long)postFrames, ob.postDumps, aliveOk, aliveBad, detail.c_str(),
              g_failed ? (" reason: " + g_reason).c_str() : "");
  return ok ? 0 : 1;
}
