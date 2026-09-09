#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "avox/AvoxPlayer.h"

using namespace avox;

// 无头 WebRTC 拉流测试 (跨平台, 无窗口无渲染): 挂内置 ZLM/WHEP 信令,
// 连接建立且收到远端首帧即 PASS, 输出 testenv 统一判定行
class RtcHeadlessOb : public IRtcEventOb {
 public:
  std::atomic<bool> connected{false};
  std::atomic<bool> firstFrame{false};

  void onConnectionState(RtcConnState state) override {
    fprintf(stderr, "[rtc] conn: %s\n", getRtcConnStateStr(state));
    if (state == RtcConnState::connected) connected = true;
  }
  void onFirstVideoFrame() override { firstFrame = true; }
};

int main(int argc, char* argv[]) {
  const char* defaultUrl =
      "http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play";
  const char* url = argc > 1 ? argv[1] : defaultUrl;
  int timeoutSec = argc > 2 ? atoi(argv[2]) : 15;
  fprintf(stderr, "[rtc] url: %s, timeout: %ds\n", url, timeoutSec);
  IRtcPlayer* player = createWebRtcPlayer();
  player->setRollType(RtcRollType::offer);
  player->setVideoDirection(RtpDirection::recvOnly);
  // 无头环境不协商音频, 避免占用/依赖音频设备
  player->setAudioDirection(RtpDirection::inactive);
  RtcHeadlessOb ob;
  player->addOb(&ob);
  IRtcEventOb* sdpAgent = createZlTestSdpAgent(player, url);
  player->addOb(sdpAgent);
  bool opened = player->open();
  // 轮询等连接+首帧, 到时判定
  bool pass = false;
  double fps = 0;
  float loss = 0;
  int32_t rtt = -1;
  for (int i = 0; i < timeoutSec * 20; ++i) {
    if (ob.connected) {
      fps = player->getFps();
      loss = player->getLossRate();
      rtt = player->getRttMs();
      if (ob.firstFrame && fps > 0) {
        pass = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  printf("[AVOX][TEST] case=webrtc-play result=%s conn=%d firstFrame=%d "
         "fps=%.1f loss=%.2f rtt=%d\n",
         pass ? "PASS" : "FAIL", ob.connected.load(), ob.firstFrame.load(),
         fps, loss, rtt);
  player->removeOb(&ob);
  player->removeOb(sdpAgent);
  player->close();
  delete player;
  return pass ? 0 : 1;
}
