// HDR10 -> SDR tone map 链路探针 (ISurfaceRender Vulkan 离屏管线):
//   1. 软解 HDR10(PQ/BT.2020+mastering SEI) 素材 -> onFrame 收图后帧(yuv420P),
//      dump PNG + 统计 Y 平面均值
//   2. 同法渲染 SDR(bt709) 参考素材对比均值: PQ 解码路径把中灰 128 抬到 ~225
//      (PQ EOTF -> ACES tone map -> BT.709 OETF), 若 transfer 误走 gamma 则均值
//      与 SDR 接近 —— 均值差 > 40 作为 tone map 生效的客观判据
//   3. -hard 档走硬解 (DX11VA P010), 观察 getDxFormat 不识别 P010 落 other 的
//      已知雷区实际表现 (计划文档 §2.2/§6.7)
// 判定: 末尾打印 case=hdrtest PASS/FAIL
// API 面现状(本探针固化的事实, 待实现):
//   - setHdrMode {auto/forceSDR/forceHDR} 全仓无实现, 宿主无法强制 SDR 输出
//   - ISurfaceRender 只有 setColorSpace, 无 setHdrMeta (HdrMeta 只在内部链路流转)
//   - IMediaPlayerOb/ISurfaceRenderOb 无 onHdrMeta 回调, 宿主拿不到峰值亮度
#include <chrono>
#include <cstdio>
#include <map>
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

}  // namespace

// 单次播放观察: onFrame 收 Vulkan 图处理后的 CPU 帧, 统计 Y 均值 + 出图
class HdrOb : public ISurfaceRenderOb {
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      fail("onFrame null buf");
      return;
    }
    ImageFormat fmt = buf->getImageFormat();
    YUVFormat yfmt = {};
    image2YUVFormat(fmt, yuvType, yfmt);
    std::lock_guard<std::mutex> lock(mtx);
    typeCount[getYuvTypeStr(yuvType)]++;
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    if (image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
      if (width == 0) {
        width = yfmt.width;
        height = yfmt.height;
        std::printf("[%s] onFrame first: %dx%d type %s\n", tag.c_str(),
                    yfmt.width, yfmt.height, getYuvTypeStr(yuvType));
      }
      // Y 平面逐帧均值 (8bit 0..255)
      int w = yfmt.width, h = yfmt.height;
      const uint8_t* y = (const uint8_t*)frame.data[0];
      int64_t sum = 0;
      for (int r = 0; r < h; r++) {
        const uint8_t* row = y + (size_t)r * frame.stride[0];
        for (int c = 0; c < w; c++) {
          sum += row[c];
        }
      }
      if (w * h > 0) {
        meanSum += (double)sum / (w * h);
        meanFrames++;
      }
      if (dumpCount < 1) {
        IImageBuffer* rgba = createImageBuffer();
        if (yuvframe2Rgba(frame, rgba)) {
          std::string path = prefix + tag + "_frame.png";
          if (saveImagePath(path.c_str(), rgba)) {
            std::printf("[%s] dump %s\n", tag.c_str(), path.c_str());
            dumpCount++;
          } else {
            fail(tag + " saveImagePath 失败");
          }
        } else {
          fail(tag + " yuvframe2Rgba 失败");
        }
        delete rgba;
      }
    } else {
      fail(tag + " image2SplitYUVFrame 失败");
    }
    delete tmp;
    frames++;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t w, int32_t h) override {}

  double meanY() const { return meanFrames ? meanSum / meanFrames : 0.0; }

  std::mutex mtx;
  std::string tag = "hdr";
  std::string prefix = "hdrtest_";
  int64_t frames = 0;
  int64_t meanFrames = 0;
  double meanSum = 0.0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t dumpCount = 0;
  std::map<std::string, int64_t> typeCount;
};

// 播放单个素材 seconds 秒, 返回是否正常出帧
bool playOnce(const char* url, bool hard, HdrOb& ob) {
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    fail("createMediaPlayer null");
    return false;
  }
  player->setHardDecode(hard);
  // 定位 10bit 软解停帧: 打开解码/渲染帧级日志
  player->getOption()->setBool("log.decoder.frame", true);
  player->getOption()->setBool("log.render.frame", true);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  addSurfaceRenderOb(sr, &ob);
  player->open(url);
  std::this_thread::sleep_for(std::chrono::seconds(4));
  player->close();
  removeSurfaceRenderOb(sr, &ob);
  delete player;
  return ob.frames > 30;
}

int main(int argc, char* argv[]) {
  const char* hdrUrl = argc > 1 ? argv[1]
                                : "assets/video/test/test_h265_hdr10_pq_640x360.mp4";
  const char* sdrUrl = argc > 2 && argv[2][0] != '-'
                           ? argv[2]
                           : "assets/video/test/test_h265_sdr_640x360.mp4";
  bool hard = false;
  std::string prefix = "hdrtest_";
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-hard") {
      hard = true;
      std::printf("mode: hard decode (P010 雷区观察档)\n");
    } else if (arg == "-prefix" && i + 1 < argc) {
      prefix = argv[++i];
    }
  }
  HdrOb hdrOb, sdrOb;
  hdrOb.tag = hard ? "hdrhard" : "hdr";
  hdrOb.prefix = prefix;
  sdrOb.tag = "sdr";
  sdrOb.prefix = prefix;
  bool hdrOk = playOnce(hdrUrl, hard, hdrOb);
  bool sdrOk = playOnce(sdrUrl, false, sdrOb);
  double hdrMean = hdrOb.meanY();
  double sdrMean = sdrOb.meanY();
  std::string hdrDist, sdrDist;
  for (const auto& [t, c] : hdrOb.typeCount) {
    hdrDist += t + ":" + std::to_string(c) + " ";
  }
  for (const auto& [t, c] : sdrOb.typeCount) {
    sdrDist += t + ":" + std::to_string(c) + " ";
  }
  std::printf("[stats] hdr frames=%lld meanY=%.1f types[%s]\n",
              (long long)hdrOb.frames, hdrMean, hdrDist.c_str());
  std::printf("[stats] sdr frames=%lld meanY=%.1f types[%s]\n",
              (long long)sdrOb.frames, sdrMean, sdrDist.c_str());
  // 判据: 双素材正常出帧 + PQ 渲染均值显著高于 SDR (tone map 链路生效)
  bool toneMapOk = hdrOb.frames > 30 && hdrMean - sdrMean > 40.0;
  bool ok = !g_failed && hdrOk && sdrOk && toneMapOk;
  std::printf(
      "[AVOX][TEST] case=hdrtest result=%s hdrMean=%.1f sdrMean=%.1f lift=%.1f%s\n",
      ok ? "PASS" : "FAIL", hdrMean, sdrMean, hdrMean - sdrMean,
      g_failed ? (" reason: " + g_reason).c_str() : "");
  return ok ? 0 : 1;
}
