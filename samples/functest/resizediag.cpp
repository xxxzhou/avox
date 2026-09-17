// 诊断: 硬解(DX11VA)在流中段分辨率变化处停帧 — 全量日志 + 渲染事件逐秒统计
// 用法: resizediag <url> [seconds=14] [hard=1]
// 判定: 事件流是否止于切换点、尺寸是否更新、解码/IO 错误回调
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxLog.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {
void onLog(int32_t level, const char* msg) {
  std::printf("[log] %s\n", msg ? msg : "");
}
}  // namespace

class DiagOb : public ISurfaceRenderOb {
 public:
  void onRender(const SurfaceRenderEvent* ev) override {
    if (!ev) {
      return;
    }
    std::lock_guard<std::mutex> lk(mtx);
    ++events;
    if (ev->generation == 0 || !ev->format.bVailid()) {
      return;
    }
    if (valid == 0) {
      std::printf("[diag] first valid event: gen=%llu %ux%u rebuilt=%u\n",
                  (unsigned long long)ev->generation,
                  (unsigned)ev->format.width, (unsigned)ev->format.height,
                  ev->rebuilt);
    } else if (ev->format.width != lastW || ev->format.height != lastH ||
               ev->generation != lastGen) {
      std::printf("[diag] change event: gen=%llu->%llu %ux%u->%ux%u rebuilt=%u\n",
                  (unsigned long long)lastGen, (unsigned long long)ev->generation,
                  lastW, lastH, (unsigned)ev->format.width,
                  (unsigned)ev->format.height, ev->rebuilt);
    }
    ++valid;
    lastW = (uint32_t)ev->format.width;
    lastH = (uint32_t)ev->format.height;
    lastGen = ev->generation;
  }
  std::mutex mtx;
  int64_t events = 0;
  int64_t valid = 0;
  uint32_t lastW = 0;
  uint32_t lastH = 0;
  uint64_t lastGen = 0;
};

class MpOb : public IMediaPlayerOb {
 public:
  void onStateChange(PlayerState preState, PlayerState state) override {
    std::printf("[diag] state %d -> %d\n", (int32_t)preState, (int32_t)state);
  }
  void onIoError(AVError error, const char* msg) override {
    std::printf("[diag] onIoError err=%d msg=%s\n", (int32_t)error,
                msg ? msg : "");
  }
  void onDecodeError(TrackType trackType, DecodeResult error) override {
    std::printf("[diag] onDecodeError track=%d err=%d\n", (int32_t)trackType,
                (int32_t)error);
  }
  void onComplete() override { std::printf("[diag] onComplete\n"); }
};

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "";
  int seconds = argc > 2 ? std::atoi(argv[2]) : 14;
  bool hard = argc > 3 ? std::atoi(argv[3]) != 0 : true;
  setLogAction(onLog);
  IMediaPlayer* player = createMediaPlayer();
  if (!player || url[0] == '\0') {
    std::printf("[diag] createMediaPlayer null or no url\n");
    return 1;
  }
  player->setHardDecode(hard);
  player->getOption()->setBool("log.decoder.frame", true);
  player->getOption()->setBool("log.render.frame", true);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::other);
  DiagOb rob;
  MpOb mob;
  addSurfaceRenderOb(sr, &rob);
  addMediaPlayerOb(player, &mob);
  std::printf("[diag] open hard=%d url=%s\n", hard ? 1 : 0, url);
  player->open(url);
  for (int i = 0; i < seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::lock_guard<std::mutex> lk(rob.mtx);
    std::printf("[progress] t=%ds events=%lld valid=%lld last=%ux%u gen=%llu\n",
                i + 1, (long long)rob.events, (long long)rob.valid, rob.lastW,
                rob.lastH, (unsigned long long)rob.lastGen);
  }
  player->close();
  removeSurfaceRenderOb(sr, &rob);
  removeMediaPlayerOb(player, &mob);
  delete player;
  std::printf("[diag] done events=%lld valid=%lld last=%ux%u\n",
              (long long)rob.events, (long long)rob.valid, rob.lastW,
              rob.lastH);
  return 0;
}
