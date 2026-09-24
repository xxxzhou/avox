// EOS 停片位泄漏复现: A 播到 completed 后直接 open 下一部(换片/同URL),
// 盯 pos 读数是否从旧停片位起跳 (panvox §六113/§六127 引擎级复现的本仓对照版)。
// 判定: 换片后首个 playing 读数 pos 应 <= 2000ms; 恒钉旧停片位 = 泄漏 FAIL。
// 用法: eosresumetest [fileA] [fileB] [-hard|-soft]
// 判定: 末尾打印 [AVOX][TEST] case=eosresumetest result=PASS|FAIL ...
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {

struct StateOb : IMediaPlayerOb {
  std::mutex mtx;
  std::chrono::steady_clock::time_point openAt;
  PlayerState state = PlayerState::none;

  void onStateChange(PlayerState pre, PlayerState cur) override {
    std::lock_guard<std::mutex> lk(mtx);
    state = cur;
  }
  bool waitState(PlayerState want, int timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx);
    return cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                       [&] { return state == want; }) &&
           state == want;
  }
  std::condition_variable cv;
};

struct FrameCountOb : ISurfaceRenderOb {
  std::atomic<int32_t> frames{0};
  void onFrame(IImageBuffer*, YuvType) override { frames++; }
};

void trip(IMediaPlayer* player, StateOb& ob, const char* tag) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - ob.openAt)
                 .count();
  std::printf("%11lldms  %s st=%d pos=%7lld dur=%7lld\n",
              (long long)now, tag, (int32_t)player->getState(),
              (long long)player->getPosition(),
              (long long)player->getDuration());
  std::fflush(stdout);
}

// 打开 url, 轮询打印 firstPollMs; 返回首个 playing 后的 pos 读数
int64_t openAndTrace(IMediaPlayer* player, StateOb& ob, const char* url,
                     const char* tag, int polls, int intervalMs) {
  std::printf("== %s open %s\n", tag, url);
  std::fflush(stdout);
  ob.openAt = std::chrono::steady_clock::now();
  player->open(url);
  int64_t firstPlayingPos = -1;
  for (int i = 0; i < polls; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    trip(player, ob, tag);
    if (firstPlayingPos < 0 && player->getState() == PlayerState::playing &&
        player->getDuration() > 0) {
      firstPlayingPos = player->getPosition();
    }
  }
  return firstPlayingPos;
}

bool waitCompleted(IMediaPlayer* player, StateOb& ob, int timeoutMs) {
  return ob.waitState(PlayerState::completed, timeoutMs);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string fileA = argc > 1 ? argv[1]
                               : "D:/Work/github/avox/assets/video/subtext_bg.mp4";
  std::string fileB = argc > 2 ? argv[2]
                               : "D:/Work/github/avox/assets/video/sherpa.mp4";
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
    std::printf("[AVOX][TEST] case=eosresumetest result=FAIL "
                "reason=createMediaPlayer-null\n");
    return 1;
  }
  player->setHardDecode(hard);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setVulkan(false);
  sr->setOffSurface(YuvType::yuv420P);
  FrameCountOb fob;
  addSurfaceRenderOb(sr, &fob);
  StateOb sob;
  addMediaPlayerOb(player, &sob);

  bool allPass = true;
  // 阶段1: 换片 (A EOS → 直接 open B)
  sob.openAt = std::chrono::steady_clock::now();
  player->open(fileA.c_str());
  if (!waitCompleted(player, sob, 60000)) {
    std::printf("[AVOX][TEST] case=eosresumetest result=FAIL reason=A-no-eos\n");
    return 1;
  }
  std::printf("A parked: st=%d pos=%lld dur=%lld frames=%d\n",
              (int32_t)player->getState(), (long long)player->getPosition(),
              (long long)player->getDuration(), fob.frames.load());
  std::fflush(stdout);
  int64_t p1 = openAndTrace(player, sob, fileB.c_str(), "switch", 14, 250);
  bool p1ok = p1 >= 0 && p1 <= 2000;
  std::printf("[phase switch] firstPlayingPos=%lld -> %s\n", (long long)p1,
              p1ok ? "PASS" : "FAIL");
  std::fflush(stdout);
  allPass = allPass && p1ok;

  // 阶段2: 同 URL (B EOS → 直接 open B)
  if (!waitCompleted(player, sob, 90000)) {
    std::printf("[AVOX][TEST] case=eosresumetest result=FAIL reason=B-no-eos\n");
    return 1;
  }
  std::printf("B parked: st=%d pos=%lld dur=%lld frames=%d\n",
              (int32_t)player->getState(), (long long)player->getPosition(),
              (long long)player->getDuration(), fob.frames.load());
  std::fflush(stdout);
  int64_t p2 = openAndTrace(player, sob, fileB.c_str(), "sameurl", 14, 250);
  bool p2ok = p2 >= 0 && p2 <= 2000;
  std::printf("[phase sameurl] firstPlayingPos=%lld -> %s\n", (long long)p2,
              p2ok ? "PASS" : "FAIL");
  std::fflush(stdout);
  allPass = allPass && p2ok;

  // 阶段3: close+open 同 URL (B EOS → close → open B)
  player->close();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  int64_t p3 = openAndTrace(player, sob, fileB.c_str(), "closeopen", 14, 250);
  bool p3ok = p3 >= 0 && p3 <= 2000;
  std::printf("[phase closeopen] firstPlayingPos=%lld -> %s\n", (long long)p3,
              p3ok ? "PASS" : "FAIL");
  std::fflush(stdout);
  allPass = allPass && p3ok;

  player->close();
  delete player;
  std::printf("[AVOX][TEST] case=eosresumetest result=%s frames=%d\n",
              allPass ? "PASS" : "FAIL", fob.frames.load());
  return allPass ? 0 : 1;
}
