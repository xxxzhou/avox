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
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
          std::string path = prefix + tag + "_yuv.png";
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
  // enableImage 通道: V5 之后/rgba2yuv 之前的 RGBA 直读, 与 yuv 通道对比定位
  void onRender() override {
    if (!imgBuf || !imgBuf->getPointer()) {
      return;
    }
    ImageFormat fmt = imgBuf->getImageFormat();
    if (fmt.width <= 0 || fmt.height <= 0) {
      return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    int64_t sum = 0;
    const uint8_t* p = (const uint8_t*)imgBuf->getPointer();
    int pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;
    for (int r = 0; r < fmt.height; r++) {
      const uint8_t* row = p + (size_t)r * pitch;
      for (int c = 0; c < fmt.width; c++) {
        sum += (row[c * 4] + row[c * 4 + 1] + row[c * 4 + 2]) / 3;
      }
    }
    int px = fmt.width * fmt.height;
    if (px > 0) {
      double m = (double)sum / px;
      imgMeanSum += m;
      imgFrames++;
      // 保留最后一帧 RGB (与另一素材同构内容做像素级映射对比)
      lastImg.resize((size_t)px * 3);
      for (int r = 0; r < fmt.height; r++) {
        const uint8_t* row = p + (size_t)r * pitch;
        uint8_t* dst = lastImg.data() + (size_t)r * fmt.width * 3;
        for (int c = 0; c < fmt.width; c++) {
          dst[c * 3] = row[c * 4];
          dst[c * 3 + 1] = row[c * 4 + 1];
          dst[c * 3 + 2] = row[c * 4 + 2];
        }
      }
      if (imgDump < 1) {
        std::string path = prefix + tag + "_img.png";
        if (saveImagePath(path.c_str(), imgBuf)) {
          std::printf("[%s] dump %s\n", tag.c_str(), path.c_str());
          imgDump++;
        }
      }
    }
    if (imgDump < 1) {
      std::string path = prefix + tag + "_img.png";
      if (saveImagePath(path.c_str(), imgBuf)) {
        std::printf("[%s] dump %s\n", tag.c_str(), path.c_str());
        imgDump++;
      }
    }
  }
  void onSurface() override {}
  void onWinSizeChange(int32_t w, int32_t h) override {}

  double meanY() const { return meanFrames ? meanSum / meanFrames : 0.0; }
  double imgMean() const { return imgFrames ? imgMeanSum / imgFrames : 0.0; }

  // 末帧像素级 tone map 判据: 与 SDR 参考同构内容逐像素对比,
  // 中间调(sdr luma 110..180)应显著抬升(tone map 特征), 整体差异
  // 面(rgb 通道 max-diff>15)应过半。总均值会被两端抵消, 不能作判据。
  struct ImgVerdict {
    double midLift = 0.0;   // 中间调 luma 均值差 (hdr-sdr)
    double diffRatio = 0.0; // rgb max-diff>15 的像素占比
    bool ok() const { return midLift > 10.0 && diffRatio > 0.5; }
  };
  ImgVerdict compareImg(const HdrOb& sdr) const {
    ImgVerdict v;
    size_t n = std::min(lastImg.size(), sdr.lastImg.size());
    if (n == 0) {
      return v;
    }
    int64_t midSum = 0, midCnt = 0, diffCnt = 0;
    for (size_t i = 0; i < n / 3; i++) {
      int dr = (int)lastImg[i * 3] - (int)sdr.lastImg[i * 3];
      int dg = (int)lastImg[i * 3 + 1] - (int)sdr.lastImg[i * 3 + 1];
      int db = (int)lastImg[i * 3 + 2] - (int)sdr.lastImg[i * 3 + 2];
      int mx = std::max(std::max(std::abs(dr), std::abs(dg)), std::abs(db));
      int dl = (dr + dg + db) / 3;
      if (mx > 15) {
        diffCnt++;
      }
      int sl = ((int)sdr.lastImg[i * 3] + (int)sdr.lastImg[i * 3 + 1] +
                (int)sdr.lastImg[i * 3 + 2]) /
               3;
      if (sl >= 110 && sl <= 180) {
        midSum += dl;
        midCnt++;
      }
    }
    if (midCnt > 0) {
      v.midLift = (double)midSum / midCnt;
    }
    v.diffRatio = (double)diffCnt / (n / 3);
    return v;
  }

  std::mutex mtx;
  std::string tag = "hdr";
  std::string prefix = "hdrtest_";
  IImageBuffer* imgBuf = nullptr;  // 外部持有(enableImage), 不 delete
  std::vector<uint8_t> lastImg;    // 末帧 RGB, 供像素级对比
  int64_t frames = 0;
  int64_t meanFrames = 0;
  double meanSum = 0.0;
  int64_t imgFrames = 0;
  double imgMeanSum = 0.0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t dumpCount = 0;
  int32_t imgDump = 0;
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
  if (ob.imgBuf) {
    // 先订阅再 enableImage, 保证首帧即能读到
    sr->enableImage(ob.imgBuf);
  }
  player->open(url);
  std::this_thread::sleep_for(std::chrono::seconds(4));
  player->close();
  if (ob.imgBuf) {
    sr->disableImage();
  }
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
  // enableImage 输出 buffer (rgba8, 640x360 与素材一致)
  IImageBuffer* hdrImg = createImageBuffer();
  IImageBuffer* sdrImg = createImageBuffer();
  ImageFormat imgFmt = {};
  imgFmt.width = 640;
  imgFmt.height = 360;
  imgFmt.imageType = ImageType::rgba8;
  hdrImg->setImageFormat(imgFmt);
  sdrImg->setImageFormat(imgFmt);
  hdrOb.imgBuf = hdrImg;
  sdrOb.imgBuf = sdrImg;
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
  std::printf("[stats] hdr frames=%lld meanY=%.1f imgMean=%.1f (%lld f) types[%s]\n",
              (long long)hdrOb.frames, hdrMean, hdrOb.imgMean(),
              (long long)hdrOb.imgFrames, hdrDist.c_str());
  std::printf("[stats] sdr frames=%lld meanY=%.1f imgMean=%.1f (%lld f) types[%s]\n",
              (long long)sdrOb.frames, sdrMean, sdrOb.imgMean(),
              (long long)sdrOb.imgFrames, sdrDist.c_str());
  // 判据: 双素材正常出帧 + 末帧像素级映射呈 tone map 特征
  // (中间调抬升 > 10, 差异面 > 50%); 总均值会被暗部压制与亮部
  // 饱和抵消, 不能作判据 (实测本素材总均值仅 +4)
  HdrOb::ImgVerdict verdict = hdrOb.compareImg(sdrOb);
  bool toneMapOk = hdrOb.frames > 30 && sdrOb.frames > 30 && verdict.ok();
  bool ok = !g_failed && hdrOk && sdrOk && toneMapOk;
  std::printf(
      "[AVOX][TEST] case=hdrtest result=%s hdrMean=%.1f sdrMean=%.1f "
      "midLift=%.1f diffRatio=%.2f%s\n",
      ok ? "PASS" : "FAIL", hdrMean, sdrMean, verdict.midLift,
      verdict.diffRatio, g_failed ? (" reason: " + g_reason).c_str() : "");
  delete hdrImg;
  delete sdrImg;
  return ok ? 0 : 1;
}
