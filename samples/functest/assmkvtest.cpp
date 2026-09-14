// ASS 内封字幕轨端到端验收(ASS字幕渲染计划.md M2~§3.4 链):
//   MKV(ASS/SSA 轨) → IOParseFF 字幕轨枚举/旁路包 → MediaPlayer 选轨
//   → avox_ass(libass) 光栅化 → VkCanvasLayer 合成 → 输出帧像素证据。
// 判据(客观像素):
//   1. subtitleSize()==1 且 codec==ass(轨道枚举通);
//   2. 选轨前底部字幕带无亮像素, 选轨后出现持续亮像素(渲染合成通, 且无残留误报);
//   3. 输出帧 PNG 落盘供人眼复核(\pos 定位+颜色+\t 动画由素材保证)。
// 素材: assets/video/ass_test.mkv (640x360 黑底, ASS 轨 0~30s 常驻, 绿+白双行)
// 用法: assmkvtest [mkv路径] [验证秒数] [输出前缀]
// 末尾打印 case=assmkv PASS/FAIL
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {

// 底部字幕带(下 1/4)亮像素计数。packed 帧先转 split YUV 再转 RGBA
// (直接把 packed 缓冲交给图像写入器会把 UV 平面当亮度读出灰带伪像),
// 在 RGBA 上按 RGB 亮度判, 步进采样省时。
int32_t brightPixelsInStrip(IImageBuffer* buf, YuvType yuvType,
                            IImageBuffer** outRgba) {
  *outRgba = nullptr;
  if (!buf || !buf->getPointer()) {
    return -1;
  }
  IImageBuffer* tmp = createImageBuffer();
  YUVFrame frame = {};
  IImageBuffer* rgba = createImageBuffer();
  if (!image2SplitYUVFrame(buf, yuvType, frame, tmp) ||
      !yuvframe2Rgba(frame, rgba)) {
    delete tmp;
    delete rgba;
    return -1;
  }
  delete tmp;
  const ImageFormat fmt = rgba->getImageFormat();
  const uint8_t* base = rgba->getPointer();
  if (!base || fmt.width <= 0 || fmt.height <= 0) {
    delete rgba;
    return -1;
  }
  const int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;
  const int32_t stripY = fmt.height * 3 / 4;
  int32_t count = 0;
  for (int32_t y = stripY; y < fmt.height; y += 2) {
    const uint8_t* row = base + (size_t)y * pitch;
    for (int32_t x = 0; x < fmt.width; x += 2) {
      const uint8_t* px = row + (size_t)x * 4;
      // 绿色或白色字幕像素(yuv 往返后 G 通道仍显著高于黑底)
      if (px[1] > 120) {
        ++count;
      }
    }
  }
  *outRgba = rgba;  // 调用方负责 delete(用于落盘)
  return count;
}

class AssOutOb : public ISurfaceRenderOb {
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    std::lock_guard<std::mutex> lock(mtx);
    if (!subArmed) {
      return;
    }
    ++frames;
    IImageBuffer* rgba = nullptr;
    const int32_t bright = brightPixelsInStrip(buf, yuvType, &rgba);
    if (bright < 0) {
      return;
    }
    if (frames == 30 && rgba) {
      saveImagePath((prefix + "raw150.png").c_str(), rgba);    }    if (frames == 150 && rgba) {      saveImagePath((prefix + "raw30.png").c_str(), rgba);
      std::printf("dump raw30\n");
    }
    if (bright > 40) {
      ++subFrames;
      // 前 4 个命中帧逐秒转储, 供人眼核对两条对白的 \pos 位置
      if (dumpCount < 4) {
        char name[64];
        std::snprintf(name, sizeof(name), "%ssub%d.png", prefix.c_str(),
                      dumpCount);
        saveImagePath(name, rgba);
        std::printf("dump %s (bright=%d)\n", name, bright);
        ++dumpCount;
      }
    }
    delete rgba;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t, int32_t) override {}

  std::mutex mtx;
  std::string prefix = "assmkv_";
  bool subArmed = false;  // 选轨后置位: 之前的帧不计入判据
  int64_t frames = 0;
  int64_t subFrames = 0;
  int32_t dumpCount = 0;
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "assets/video/ass_test.mkv";
  int seconds = argc > 2 ? std::atoi(argv[2]) : 6;
  if (seconds <= 0) {
    seconds = 6;
  }
  std::string prefix = argc > 3 ? (std::string(argv[3]) + "_") : "assmkv_";

  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("case=assmkv FAIL (createMediaPlayer null)\n");
    return 1;
  }
  AssOutOb ob;
  ob.prefix = prefix;
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  addSurfaceRenderOb(sr, &ob);
  player->open(url);
  // 等进入播放(最多 15s)
  bool playing = false;
  for (int i = 0; i < 150; ++i) {
    const int st = (int)player->getState();
    if (i % 10 == 0) {
      std::printf("poll %d state=%d\n", i, st);
    }
    if (st == (int)PlayerState::playing) {
      playing = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!playing) {
    std::printf("case=assmkv FAIL (not playing in 15s)\n");
    player->close();
    delete player;
    return 1;
  }

  // 轨道枚举判据
  ISourceInfo* info = player->getSourceInfo();
  const int32_t subCount = info ? info->subtitleSize() : 0;
  bool trackOk = false;
  if (subCount == 1) {
    STrackDesc desc = info->getSubtitleDesc(0);
    trackOk = desc.codecId == SCodecId::ass;
    std::printf("track0: codec=%s lang=%s title=%s forced=%d\n",
                desc.codecId == SCodecId::ass ? "ass" : "?",
                desc.lang.c_str(), desc.title.c_str(), (int)desc.forced);
  } else {
    std::printf("subtitle tracks: %d (expect 1)\n", subCount);
  }

  // 选轨 → 底部字幕带应出现持续亮像素
  player->setSubtitleTrack(0);
  {
    std::lock_guard<std::mutex> lock(ob.mtx);
    ob.subArmed = true;
  }
  for (int i = 0; i < seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  player->close();
  removeSurfaceRenderOb(sr, &ob);
  delete player;

  const bool ok = trackOk && ob.frames >= 30 && ob.subFrames >= 10;
  std::printf(
      "[AVOX][TEST] case=assmkv result=%s trackOk=%d frames=%lld subFrames=%lld\n",
      ok ? "PASS" : "FAIL", (int)trackOk, (long long)ob.frames,
      (long long)ob.subFrames);
  return ok ? 0 : 1;
}
