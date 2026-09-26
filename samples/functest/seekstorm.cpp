// seek 风暴回归: 模拟进度条拖动(密集连发 seek, 不等恢复)后是否楔死。
// 动机: 改后缀 FLV(flvEstimateSeek 快速 seek)用户实测拖动 seek 后画面冻住、
//       重开也不动, 而单次 seektest 全绿 —— 密集 seek 是单测没覆盖的路径。
// 判定: 风暴结束后 20s 内回到 playing, 且随后 5s 帧计数持续推进(≥60 帧)。
// 用法: seekstorm <url> [stormCount=24] [intervalMs=120] [-hard|-soft]
// 判定: 末尾打印 [AVOX][TEST] case=seekstorm result=PASS|FAIL ...
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"

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
  std::fflush(stdout ? stdout : stderr);
}

// 帧计数: 只数不下盘, 风暴后帧流是否恢复是唯一判据
struct FrameOb : ISurfaceRenderOb {
  std::atomic<int64_t> frames{0};
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    (void)buf;
    (void)yuvType;
    ++frames;
  }
  void onSurface() override {}
  void onRender(const SurfaceRenderEvent*) override {}
  void onWinSizeChange(int32_t width, int32_t height) override {}
};

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

}  // namespace

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "";
  if (!url || !*url) {
    std::printf("用法: seekstorm <url> [stormCount=24] [intervalMs=120] "
                "[-hard|-soft]\n");
    return 2;
  }
  int stormCount = argc > 2 ? std::atoi(argv[2]) : 24;
  int intervalMs = argc > 3 ? std::atoi(argv[3]) : 120;
  bool hard = true;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-soft") {
      hard = false;
    } else if (arg == "-hard") {
      hard = true;
    }
  }

  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[AVOX][TEST] case=seekstorm result=FAIL "
                "reason=createMediaPlayer-null\n");
    return 1;
  }
  player->setHardDecode(hard);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setVulkan(false);
  sr->setOffSurface(YuvType::yuv420P);
  FrameOb fob;
  addSurfaceRenderOb(sr, &fob);
  StateOb sob;
  addMediaPlayerOb(player, &sob);

  std::printf("mode: %s decode offscreen-yuv420P\nseekstorm open %s\n",
              hard ? "hard" : "soft", url);
  std::fflush(stdout);
  player->open(url);
  if (!sob.waitState(PlayerState::playing, 30000)) {
    fail("起播 30s 未到 playing");
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  int64_t dur = player->getDuration();
  if (dur <= 0) {
    dur = 5400 * 1000;
  }
  std::printf("duration=%lldms, storm=%d x %dms\n", (long long)dur, stormCount,
              intervalMs);
  std::fflush(stdout);

  // 风暴: 前后大幅往复(LCG 伪随机), 故意跨过已看区间(索引路)与未看区间
  // (估算路)两种快速 seek 分支; 间隔内不等恢复, 复刻进度条拖动节奏
  const auto stormStart = std::chrono::steady_clock::now();
  uint32_t seed = 20260926;
  for (int32_t i = 0; i < stormCount; i++) {
    seed = seed * 1664525u + 1013904223u;
    const int64_t target = 30000 + (int64_t)(seed % 1000u) *
                                     ((dur - 90000) / 1000);
    std::printf("storm[%d] seek -> %lldms\n", i, (long long)target);
    std::fflush(stdout);
    player->seek(target);
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
  }
  const int64_t stormMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - stormStart)
                              .count();
  const bool back = sob.waitState(PlayerState::playing, 20000);
  if (!back) {
    fail("风暴后 20s 未恢复 playing");
  }
  const int64_t framesBefore = fob.frames.load();
  std::this_thread::sleep_for(std::chrono::seconds(5));
  const int64_t framesAfter = fob.frames.load();
  const int64_t tail = framesAfter - framesBefore;

  player->close();
  removeMediaPlayerOb(player, &sob);
  removeSurfaceRenderOb(sr, &fob);
  delete player;

  const bool ok = !g_failed && back && tail >= 60;
  std::printf("[AVOX][TEST] case=seekstorm result=%s stormMs=%lld tail=%lld "
              "reason=%s\n",
              ok ? "PASS" : "FAIL", (long long)stormMs, (long long)tail,
              g_failed ? g_reason.c_str() : "-");
  return ok ? 0 : 1;
}
