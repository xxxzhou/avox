// 玻璃到玻璃延迟采样: onFrame 解码帧 → RGBA → 周期覆盖存 <prefix>cur.png。
// 跳过首帧后每 500ms 覆盖写一次, 文件 mtime 即捕获时刻; 外部脚本读画面里
// 源端烙印的生成时间戳, latency = mtime - 烙印值。
// 用法: latprobe <url> [duration_s] [prefix]
// 判定: 末尾打印 case=latprobe PASS/FAIL
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox/module/OptionKey.hpp"

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
}
}  // namespace

class LatProbeOb : public ISurfaceRenderOb {
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    const auto now = std::chrono::steady_clock::now();
    if (firstFrame == std::chrono::steady_clock::time_point{}) {
      firstFrame = now;
    }
    // 跳过前 3s (起播抖动), 之后每 500ms 覆盖存一张稳态帧
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - firstFrame)
            .count() < 3000 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSave)
                .count() < 500) {
      frames++;
      return;
    }
    lastSave = now;
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    if (image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
      IImageBuffer* rgba = createImageBuffer();
      if (yuvframe2Rgba(frame, rgba)) {
        std::string path = prefix + "cur.png";
        if (saveImagePath(path.c_str(), rgba)) {
          saved++;
        } else {
          fail("saveImagePath 失败");
        }
      } else {
        fail("yuvframe2Rgba 失败");
      }
      delete rgba;
    } else {
      fail("image2SplitYUVFrame 失败 (tmp 已给)");
    }
    delete tmp;
    frames++;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t width, int32_t height) override {}

  std::mutex mtx;
  std::chrono::steady_clock::time_point firstFrame{};
  std::chrono::steady_clock::time_point lastSave{};
  int64_t frames = 0;
  int64_t saved = 0;
  std::string prefix = "lat_";
};

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "rtsp://127.0.0.1:554/live/test";
  int seconds = argc > 2 ? std::atoi(argv[2]) : 12;
  if (seconds <= 0) {
    seconds = 12;
  }
  std::string prefix = argc > 3 ? (std::string(argv[3]) + "_") : "lat_";
  // argv[4]: "low"=低延迟模式(默认, AVOX_MP_LOW_LATENCY_BOOL + delay 0),
  //          "default"=默认缓冲口径 (对比用)
  std::string mode = argc > 4 ? argv[4] : "low";
  LatProbeOb ob;
  ob.prefix = prefix;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[AVOX][TEST] case=latprobe result=FAIL reason=createMediaPlayer-null\n");
    return 1;
  }
  if (mode == "low") {
    auto* opt = player->getOption();
    opt->setBool(AVOX_MP_LOW_LATENCY_BOOL, true);
    opt->setNumber(AVOX_MP_LL_SPEED_DOUBLE, 1.2);
    opt->setInt(AVOX_MP_DELAY_MS_INT, 0);
  }
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  addSurfaceRenderOb(sr, &ob);
  player->open(url);
  for (int i = 0; i < seconds; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  player->close();
  removeSurfaceRenderOb(sr, &ob);
  delete player;
  bool ok = !g_failed && ob.saved >= 3;
  std::printf("[AVOX][TEST] case=latprobe result=%s frames=%lld saved=%lld%s\n",
              ok ? "PASS" : "FAIL", (long long)ob.frames, (long long)ob.saved,
              g_failed ? (" reason: " + g_reason).c_str() : "");
  return ok ? 0 : 1;
}
