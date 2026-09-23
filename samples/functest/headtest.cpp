// 起播头几秒逐帧体检: 复现「打开文件开头花屏数秒后自愈」的用户场景。
// 动机: 3.wmv(WMV3) 用户报起播约 3 秒花屏后恢复; seektest 只抽起播头 2 帧
//       (pre) 且后续走 seek, 抓不到完整自愈时间线 —— 本探针不 seek, 起播后
//       连续统计每一帧 (luma/std/判定), 每 N 帧落 PNG 留人眼复判。
// 车道: 离屏 yuv420P onFrame (seektest 同款, 抓解码真值)。默认硬解, -soft 隔离。
// 用法: headtest <url> [outprefix] [-hard|-soft] [watchSec]
// 判定: 打印逐帧行, 末尾汇总 [AVOX][TEST] case=headtest ... (恢复帧号/灰帧数)
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

struct StateOb : IMediaPlayerOb {
  std::mutex mtx;
  std::condition_variable cv;
  PlayerState state = PlayerState::none;
  bool ioErr = false;
  bool waitState(PlayerState want, int timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return state == want || ioErr; }) &&
           state == want;
  }
  void onStateChange(PlayerState pre, PlayerState cur) override {
    std::lock_guard<std::mutex> lk(mtx);
    state = cur;
    cv.notify_all();
  }
  void onIoError(AVError error, const char* msg) override {
    std::lock_guard<std::mutex> lk(mtx);
    ioErr = true;
    cv.notify_all();
  }
};

// ── 逐帧观察: 每帧统计一行, 每 dumpEvery 帧落 PNG ──
class HeadOb : public ISurfaceRenderOb {
 public:
  ISurfaceRender* sr = nullptr;
  std::string prefix = "head_";
  int dumpEvery = 20;
  int watchFrames = 320;
  std::chrono::steady_clock::time_point t0;

  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      fail("onFrame null buf");
      return;
    }
    int64_t idx = frames;
    frames++;
    if (idx >= watchFrames) {
      return;
    }
    if (idx == 0) {
      t0 = std::chrono::steady_clock::now();
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // 统计走 RGBA: onFrame 的 buf 是 planar yuv420P, 直接按 RGB 读无效
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    ImageStats st;
    const char* why = "convert-fail";
    if (image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
      IImageBuffer* rgba = createImageBuffer();
      if (yuvframe2Rgba(frame, rgba, sr->getOutColorSpace())) {
        st = analyzeImage(rgba);
        why = aliveWhy(st);
        if (idx % dumpEvery == 0) {
          std::string path = prefix + std::to_string(idx) + ".png";
          saveImagePath(path.c_str(), rgba);
        }
      }
      delete rgba;
    }
    delete tmp;
    if (std::strcmp(why, "alive") != 0) {
      badFrames++;
      if (firstBadFrame < 0) {
        firstBadFrame = idx;
      }
      lastBadFrame = idx;
    } else if (firstGoodAfterBad < 0 && firstBadFrame >= 0) {
      firstGoodAfterBad = idx;
    }
    std::printf("frame %3lld t=%5.2fs luma=%6.1f std=%5.1f top=%.2f %s\n", (long long)idx, sec,
                st.meanLuma, st.stdLuma, st.topColorRatio, why);
    if (idx == watchFrames - 1) {
      std::fflush(stdout);
    }
  }
  void onSurface() override {}
  void onRender(const SurfaceRenderEvent*) override {}
  void onWinSizeChange(int32_t width, int32_t height) override {}

  void dump(IImageBuffer* buf, YuvType yuvType, int64_t idx) {
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    if (!image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
      delete tmp;
      return;
    }
    IImageBuffer* rgba = createImageBuffer();
    if (yuvframe2Rgba(frame, rgba, sr->getOutColorSpace())) {
      std::string path = prefix + std::to_string(idx) + ".png";
      saveImagePath(path.c_str(), rgba);
    }
    delete rgba;
    delete tmp;
  }

  std::atomic<int64_t> frames{0};
  int64_t badFrames = 0;
  int64_t firstBadFrame = -1;
  int64_t lastBadFrame = -1;
  int64_t firstGoodAfterBad = -1;
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "";
  if (!url || !*url) {
    std::printf("用法: headtest <url> [outprefix] [-hard|-soft] [watchFrames]\n");
    return 2;
  }
  std::string prefix = argc > 3 ? (std::string(argv[2]) + "_") : "head_";
  bool hard = true;
  int watchFrames = 320;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-soft") {
      hard = false;
    } else if (arg == "-hard") {
      hard = true;
    } else if (arg[0] != '-' && i == 4) {
      watchFrames = std::atoi(argv[4]);
    }
  }

  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[AVOX][TEST] case=headtest result=FAIL reason=createMediaPlayer-null\n");
    return 1;
  }
  player->setHardDecode(hard);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setVulkan(false);
  sr->setOffSurface(YuvType::yuv420P);
  HeadOb ob;
  ob.sr = sr;
  ob.prefix = prefix;
  ob.watchFrames = watchFrames;
  addSurfaceRenderOb(sr, &ob);
  StateOb sob;
  addMediaPlayerOb(player, &sob);

  std::printf("mode: %s decode offscreen-yuv420P\nheadtest open %s\n", hard ? "hard" : "soft", url);
  std::fflush(stdout);
  player->open(url);
  if (!sob.waitState(PlayerState::playing, 30000)) {
    fail("起播 30s 未到 playing");
  }
  // 观察窗口: 320 帧 @30fps ≈ 10.7s, 覆盖 4s GOP 两轮
  while (ob.frames < ob.watchFrames && !g_failed) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  bool ok = !g_failed && ob.frames > 0;
  std::printf(
      "[AVOX][TEST] case=headtest result=%s frames=%lld bad=%lld firstBad=%lld lastBad=%lld "
      "recoverAt=%lld\n",
      ok ? "DONE" : "FAIL", (long long)ob.frames.load(), (long long)ob.badFrames,
      (long long)ob.firstBadFrame, (long long)ob.lastBadFrame,
      (long long)ob.firstGoodAfterBad);
  std::fflush(stdout);
  player->close();
  removeMediaPlayerOb(player, &sob);
  removeSurfaceRenderOb(sr, &ob);
  delete player;
  return ok ? 0 : 1;
}
