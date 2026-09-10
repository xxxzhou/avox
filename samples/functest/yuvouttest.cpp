// 帧布局契约运行时验证 (IImageBuffer 恒 packed / YUVFrame 恒 split):
//   1. onFrame(IImageBuffer*, YuvType) 收 packed 帧: 校验 rowPitch/bufferSize,
//      抽 2 帧经 image2SplitYUVFrame → yuvframe2Rgba 存 PNG (packed→split→rgba 全链)
//   2. onFrame 与 getMuxer(true) 转码录制同时开: 复现历史"双路取帧"花屏场景
//      (onRenderOut 回调与 pushFrame 录制各取一帧), 两路输出都必须色度正确
//   3. screenShot 非对齐宽度截图存 PNG
// 建议输入: yuv420P 且宽度非 16/32 对齐 (如 622x482, 解码 linesize 对齐到 640),
//          此时 packed 420P 的 UV 物理行是 [偶|奇|pad], 与 split 布局有分歧
// 用法: yuvouttest <url> [seconds] [outprefix]
// 判定: 末尾打印 case=yuvout PASS/FAIL
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

}  // namespace

class YuvOutOb : public ISurfaceRenderOb {
 public:
  // onFrame: packed 帧契约校验 + 抽帧转 RGBA (验证 packed→split→rgba 全链)
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      fail("onFrame null buf");
      return;
    }
    ImageFormat fmt = buf->getImageFormat();
    YUVFormat yfmt = {};
    image2YUVFormat(fmt, yuvType, yfmt);
    std::lock_guard<std::mutex> lock(mtx);
    if (frames == 0) {
      std::printf("onFrame first: %dx%d (rowPitch %d) type %s bufSize %d\n",
                  yfmt.width, yfmt.height, fmt.rowPitch, getYuvTypeStr(yuvType),
                  buf->getBufferSize());
      // 契约: rowPitch >= width, 缓冲容纳整帧
      infoOk = fmt.rowPitch >= fmt.width &&
               buf->getBufferSize() >= getYuvFrameSize(yfmt, fmt.rowPitch);
      if (!infoOk) {
        fail("packed 契约破坏: rowPitch<width 或 bufSize 不足");
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
            std::printf("onFrame dump %s\n", path.c_str());
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
    }
    frames++;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t width, int32_t height) override {}

  std::mutex mtx;
  int64_t frames = 0;
  bool infoOk = false;
  std::string prefix = "yuvout_";
};

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "src622.mp4";
  int seconds = argc > 2 ? std::atoi(argv[2]) : 5;
  if (seconds <= 0) {
    seconds = 5;
  }
  std::string prefix = argc > 3 ? (std::string(argv[3]) + "_") : "yuvout_";
  YuvOutOb ob;
  ob.prefix = prefix;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("case=yuvout FAIL (createMediaPlayer null)\n");
    return 1;
  }
  ISurfaceRender* sr = player->getSurfaceRender();
  // yuv420P 输出: rowPitch != width 时 packed 与 split 有分歧, 正是契约要守的场景
  sr->setOffSurface(YuvType::yuv420P);
  addSurfaceRenderOb(sr, &ob);
  player->open(url);
  // 1s 后开转码录制: 与 onFrame 观察者并存 = 历史双路取帧花屏场景
  std::this_thread::sleep_for(std::chrono::seconds(1));
  IMediaMuxer* muxer = player->getMuxer(true);
  muxer->setMuxerType(MuxerType::ffmpeg);
  muxer->setVideoCodec(VCodecId::h264);
  std::string recPath = prefix + "rec.mp4";
  bool recOpen = muxer->open(recPath.c_str());
  if (!recOpen) {
    fail("muxer open 失败");
  }
  // 中途截图
  std::this_thread::sleep_for(std::chrono::seconds(1));
  IImageBuffer* shot = createImageBuffer();
  if (sr->screenShot(shot)) {
    std::string shotPath = prefix + "shot.png";
    if (saveImagePath(shotPath.c_str(), shot)) {
      std::printf("screenshot %s\n", shotPath.c_str());
    } else {
      fail("截图存盘失败");
    }
  } else {
    fail("screenShot 失败");
  }
  delete shot;
  // 继续录制到时长结束
  for (int i = 2; i < seconds; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  if (recOpen) {
    muxer->close();
  }
  player->close();
  removeSurfaceRenderOb(sr, &ob);
  delete player;
  // 判定
  std::string recFile = prefix + "rec.mp4";
  bool ok = !g_failed && ob.infoOk && ob.frames >= 10 &&
            fileExists(recFile) && fileExists(prefix + "frame0.png") &&
            fileExists(prefix + "shot.png");
  std::printf("frames=%lld rec=%d case=yuvout %s%s\n", (long long)ob.frames,
              (int)fileExists(recFile), ok ? "PASS" : "FAIL",
              g_failed ? (" reason: " + g_reason).c_str() : "");
  return ok ? 0 : 1;
}
