// HDR10 -> SDR tone map 链路探针 (ISurfaceRender Vulkan 离屏管线):
//   1. 软解 HDR10(PQ/BT.2020+mastering SEI) 素材 -> onFrame 收图后帧(yuv420P),
//      dump PNG + 统计 Y 平面均值
//   2. 同法渲染 SDR(bt709) 参考素材对比均值: PQ 解码路径把中灰 128 抬到 ~225
//      (PQ EOTF -> tone map(软膝默认 / BT.2390 EETF) -> BT.709 OETF), 若 transfer
//      误走 gamma 则均值与 SDR 接近 —— 均值差 > 40 作为 tone map 生效的客观判据
//   3. 曲线 A/B(2026-10-12, 画质深化方案 ①): 环境变量 AVOX_TONEMAP_CURVE=1|bt2390
//      下发 BT.2390 EETF, 不设=软膝基线; 渲染后像素经 screenShot 通道取 5 段块值。
//      实测口径: 本机 mac 的 onFrame CPU-YUV 通道为**解码直出**(锚点块=原样 PQ 码值)、
//      enableImage 通道零值, 两通道都到不了 tone map 输出, 故以 screenShot 为准。
//      锚点素材实测: 软膝 0/183/215/251/251 vs EETF 0/189/225/251/251(100/203nit
//      块 +6/+10 码, 同数值预测方向与幅值)
//      **本机渲染腿=Metal**(setToneMap 日志出自 MetalRender): 故 mac 侧实测覆盖
//      Metal 腿; VK/DX11 腿同源同参数但需各自平台复测
//   5. dither A/B(2026-10-12, 画质深化方案 ②): AVOX_DITHER=<强度> 下发输出端抖动,
//      配仍静态 PQ 渐变素材(码 72..91, 落在 8bit 台阶最宽带)量测台阶结构:
//      默认/显式 0 逐位一致; 1.0 时最长恒定段 66px→34px、跳变数 50→110, 2.0 时 26px/144;
//      静态 SDR 渐变 0 vs 2.0 逐位一致(纯 SDR 链不参与)
//   4. -hard 档走硬解 (DX11VA P010), 观察 getDxFormat 不识别 P010 落 other 的
//      已知雷区实际表现 (计划文档 §2.2/§6.7)
//   5. API 面验证: IMediaPlayerOb::onHdrMeta 宿主回调(HDR 流回调/SDR 流不回调)
//      + ISurfaceRender::setHdrMode(forceHDR 直通: 对 follow 中间调大幅回落,
//      与 SDR 参考接近重合)
// 判定: 末尾打印 case=hdrtest PASS/FAIL
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
  ISurfaceRender* sr = nullptr;
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
      // 10bit 输出采样(2026-10-12, tone map 曲线验证): 原始 u16 域均值 + 首帧
      // 5 段均值(锚点素材横向 5 块) —— 判别输出域(PQ 直通 vs tone map 后)
      // 与两条曲线的差异
      if (yuvType == YuvType::yuv420P10 || yuvType == YuvType::p010) {
        int64_t sum16 = 0;
        int64_t blk[5] = {0, 0, 0, 0, 0};
        for (int r = 0; r < h; r++) {
          const unsigned short* row16 = (const unsigned short*)(
              (const unsigned char*)frame.data[0] + (size_t)r * frame.stride[0]);
          for (int c = 0; c < w; c++) {
            sum16 += row16[c];
            blk[(int64_t)c * 5 / (w > 0 ? w : 1)] += row16[c];
          }
        }
        if (w * h > 0) {
          mean16Sum += (double)sum16 / (w * h);
          mean16Frames++;
          if (mean16Frames == 1) {
            std::printf("[%s] meanY16 first=%.1f blk16=", tag.c_str(),
                        (double)sum16 / (w * h));
            for (int i = 0; i < 5; i++) {
              int64_t c0 = (int64_t)w * i / 5;
              int64_t c1 = (int64_t)w * (i + 1) / 5;
              std::printf("%.0f%s",
                          (double)blk[i] / std::max<int64_t>(c1 - c0, 1),
                          i == 4 ? "\n" : "/");
            }
          }
        }
      }
      if (dumpCount < 1) {
        IImageBuffer* rgba = createImageBuffer();
        if (yuvframe2Rgba(frame, rgba, sr->getOutColorSpace())) {
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
  void onRender(const SurfaceRenderEvent*) override {
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
        // 采样帧 RGB (与另一素材同构内容做像素级映射对比)。
        // captureIdx>0 时按渲染帧序号采样: forceHDR 档等首帧后才切模式,
        // 末帧的流内时间与 follow/SDR 播错位, 移动图案会把像素对比变噪声
        if (captureIdx < 0 || (!captured && imgFrames >= captureIdx)) {
          captured = true;
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
    // 硬信号是 diffRatio(无 tone map 时 ≈0.02); midLift 仅作方向哨兵
    // (实证跨素材 9.4~16 波动, >10 会贴边误报)
    bool ok() const { return midLift > 3.0 && diffRatio > 0.5; }
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
  std::vector<uint8_t> lastImg;    // 采样帧 RGB, 供像素级对比
  // 采样帧序号: -1=最后一帧; >0 时取该渲染帧序号的帧(跨播放内容对齐)
  int32_t captureIdx = -1;
  bool captured = false;
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
  // 10bit 输出域采样(2026-10-12, tone map 曲线验证): 原始 u16 均值
  int64_t mean16Frames = 0;
  double mean16Sum = 0.0;
  double meanY16() const { return mean16Frames ? mean16Sum / mean16Frames : 0.0; }
};

// 宿主观察者: 捕获 IMediaPlayerOb::onHdrMeta (流级属性, 每次开流至多一次)
class MetaOb : public IMediaPlayerOb {
 public:
  void onHdrMeta(const HdrMeta& hdrMeta) override {
    std::lock_guard<std::mutex> lock(mtx);
    seen = true;
    maxLuminance = hdrMeta.maxLuminance;
    maxCLL = hdrMeta.maxCLL;
  }
  std::mutex mtx;
  bool seen = false;
  uint32_t maxLuminance = 0;
  uint32_t maxCLL = 0;
};

// 播放单个素材 seconds 秒, 返回是否正常出帧
// mode 非 follow 时等首帧后再切(走运行时 UBO 重传路径, 图未建时设置会被丢弃)
bool playOnce(const char* url, bool hard, HdrOb& ob,
              HdrMode mode = HdrMode::follow, MetaOb* metaOb = nullptr) {
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
  ob.sr = sr;
  sr->setOffSurface(YuvType::yuv420P);
  // tone map 曲线/dither 开关(2026-10-12, 画质深化方案 ①/② 验证):
  // AVOX_TONEMAP_CURVE=1|bt2390 下发 BT.2390 EETF; AVOX_DITHER=<强度> 下发输出端抖动。
  // 未设=默认软膝 + dither 关(逐位回归基线)
  ToneMapParamet tm = {};
  const char* curveEnv = std::getenv("AVOX_TONEMAP_CURVE");
  if (curveEnv &&
      (std::string(curveEnv) == "1" || std::string(curveEnv) == "bt2390")) {
    tm.curve = ToneMapCurve::bt2390;
  }
  const char* ditherEnv = std::getenv("AVOX_DITHER");
  if (ditherEnv) {
    tm.ditherStrength = (float)std::atof(ditherEnv);
  }
  if (tm.curve != ToneMapCurve::softKnee || tm.ditherStrength > 0.0f) {
    sr->setToneMap(tm);
    std::printf("[tonemap] curve=%d dither=%.2f (env)\n", (int32_t)tm.curve,
                tm.ditherStrength);
  }
  addSurfaceRenderOb(sr, &ob);
  if (metaOb) {
    addMediaPlayerOb(player, metaOb);
  }
  if (ob.imgBuf) {
    // 先订阅再 enableImage, 保证首帧即能读到
    sr->enableImage(ob.imgBuf);
  }
  player->open(url);
  if (mode != HdrMode::follow) {
    for (int32_t i = 0; i < 40 && ob.frames < 3; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sr->setHdrMode(mode);
    std::printf("[mode] set %d after %lld frames\n", (int32_t)mode,
                (long long)ob.frames);
  }
  std::this_thread::sleep_for(std::chrono::seconds(4));
  // tone map 曲线/dither 验证(2026-10-12): 经 screenShot 取渲染后像素 —— onFrame 的
  // CPU-YUV 通道在 mac 实测为解码直出(锚点块=原样 PQ 码值), enableImage 通道
  // 零值, 两者都到不了 tone map 输出; screenShot 走渲染腿输出口(本机 mac=Metal)
  {
    IImageBuffer* shot = createImageBuffer();
    ImageFormat sf = {};
    sf.width = 640;
    sf.height = 360;
    sf.imageType = ImageType::rgba8;
    shot->setImageFormat(sf);
    if (sr->screenShot(shot)) {
      std::string path = "shot_" + ob.tag + ".png";
      saveImagePath(path.c_str(), shot);
      const unsigned char* p = (const unsigned char*)shot->getPointer();
      ImageFormat f2 = shot->getImageFormat();
      if (p && f2.width > 0 && f2.height > 0) {
        int pitch = f2.rowPitch > 0 ? f2.rowPitch : f2.width * 4;
        int64_t blk[5] = {0, 0, 0, 0, 0};
        int64_t cnt[5] = {0, 0, 0, 0, 0};
        for (int r = 0; r < f2.height; r++) {
          const unsigned char* row = p + (size_t)r * pitch;
          for (int c = 0; c < f2.width; c++) {
            int b = (int)c * 5 / f2.width;
            blk[b] += (row[c * 4] + row[c * 4 + 1] + row[c * 4 + 2]) / 3;
            cnt[b]++;
          }
        }
        std::printf("[%s] shot %s blk=", ob.tag.c_str(), path.c_str());
        for (int i = 0; i < 5; i++) {
          std::printf("%.0f%s", cnt[i] ? (double)blk[i] / cnt[i] : 0.0,
                      i == 4 ? "\n" : "/");
        }
        // 台阶统计(画质深化方案 ② 判据): 取中部一行 luma, 数恒定平台段与最长平台。
        // dither 生效应把长平台打散成 1~2px 交替, 最长平台显著下降;
        // 8bit 纯 SDR 链/直通链不参与, 故本行为默认档应保持不变
        {
          int r = f2.height / 2;
          const unsigned char* row = p + (size_t)r * pitch;
          int runs = 0, maxRun = 0, cur = 0, prev = -1, distinct = 0;
          bool seen[256] = {false};
          for (int c = 0; c < f2.width; c++) {
            int l = (row[c * 4] + row[c * 4 + 1] + row[c * 4 + 2]) / 3;
            if (!seen[l]) {
              seen[l] = true;
              distinct++;
            }
            if (l == prev) {
              cur++;
            } else {
              runs++;
              cur = 1;
              prev = l;
            }
            if (cur > maxRun) {
              maxRun = cur;
            }
          }
          std::printf("[%s] row%d runs=%d maxRun=%dpx distinct=%d\n",
                      ob.tag.c_str(), r, runs, maxRun, distinct);
        }
      }
    } else {
      std::printf("[%s] screenShot failed\n", ob.tag.c_str());
    }
    delete shot;
  }
  player->close();
  if (ob.imgBuf) {
    sr->disableImage();
  }
  removeSurfaceRenderOb(sr, &ob);
  delete player;
  return ob.frames > 30;
}

int main(int argc, char* argv[]) {
  // 默认素材取自同级 avox-test 仓 (私有): 本仓 assets/video/test/ 副本已删 (2026-09-16),
  // 素材只有一份; 没有该仓时按提示传参: hdrtest <hdr.mp4> <sdr.mp4>
  const char* hdrUrl = argc > 1 ? argv[1]
                                : "../avox-test/assets/video/test_h265_hdr10_pq_640x360.mp4";
  const char* sdrUrl = argc > 2 && argv[2][0] != '-'
                           ? argv[2]
                           : "../avox-test/assets/video/test_h265_sdr_640x360.mp4";
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
  // 三档播放都取第 90 渲染帧做像素对比: 内容按帧序天然对齐
  hdrOb.captureIdx = 90;
  sdrOb.captureIdx = 90;
  // forceHDR 档: 直通输出与元数据回调的验证通道
  HdrOb forceOb;
  forceOb.tag = "forcehdr";
  forceOb.prefix = prefix;
  IImageBuffer* forceImg = createImageBuffer();
  forceImg->setImageFormat(imgFmt);
  forceOb.imgBuf = forceImg;
  forceOb.captureIdx = 90;
  MetaOb hdrMetaOb, sdrMetaOb, forceMetaOb;
  bool hdrOk = playOnce(hdrUrl, hard, hdrOb, HdrMode::follow, &hdrMetaOb);
  bool sdrOk = playOnce(sdrUrl, false, sdrOb, HdrMode::follow, &sdrMetaOb);
  bool forceOk = playOnce(hdrUrl, false, forceOb, HdrMode::forceHDR, &forceMetaOb);
  double hdrMean = hdrOb.meanY();
  double sdrMean = sdrOb.meanY();
  std::string hdrDist, sdrDist;
  for (const auto& [t, c] : hdrOb.typeCount) {
    hdrDist += t + ":" + std::to_string(c) + " ";
  }
  for (const auto& [t, c] : sdrOb.typeCount) {
    sdrDist += t + ":" + std::to_string(c) + " ";
  }
  std::printf("[stats] hdr frames=%lld meanY=%.1f meanY16=%.1f imgMean=%.1f (%lld f) types[%s]\n",
              (long long)hdrOb.frames, hdrMean, hdrOb.meanY16(), hdrOb.imgMean(),
              (long long)hdrOb.imgFrames, hdrDist.c_str());
  std::printf("[stats] sdr frames=%lld meanY=%.1f imgMean=%.1f (%lld f) types[%s]\n",
              (long long)sdrOb.frames, sdrMean, sdrOb.imgMean(),
              (long long)sdrOb.imgFrames, sdrDist.c_str());
  // 判据: 双素材正常出帧 + 末帧像素级映射呈 tone map 特征
  // (中间调抬升 > 10, 差异面 > 50%); 总均值会被暗部压制与亮部
  // 饱和抵消, 不能作判据 (实测本素材总均值仅 +4)
  HdrOb::ImgVerdict verdict = hdrOb.compareImg(sdrOb);
  bool toneMapOk = hdrOb.frames > 30 && sdrOb.frames > 30 && verdict.ok();
  // 宿主回调契约: HDR 流(两次)必回调且峰值=素材注入值, SDR 流不回调
  bool metaOk = hdrMetaOb.seen && forceMetaOb.seen && !sdrMetaOb.seen &&
                hdrMetaOb.maxLuminance == 1000;
  if (!metaOk) {
    fail("onHdrMeta 回调异常: hdr=" + std::to_string(hdrMetaOb.seen) +
         "/" + std::to_string(hdrMetaOb.maxLuminance) +
         " force=" + std::to_string(forceMetaOb.seen) +
         " sdrSeen=" + std::to_string(sdrMetaOb.seen));
  }
  // forceHDR 直通的三向关系: follow≠sdr(tone map 开,上面 verdict 已判),
  // force≠follow(diffRatio 过半, 模式确实生效), force≈sdr(直通还原内容,
  // 本素材内容即 SDR 采样值, 仅差 bt2020/bt709 矩阵)。
  // 注意不用 midLift<阈值: ACES 压暗与抬升在频带内正负抵消, 均值类判据失真
  HdrOb::ImgVerdict vsFollow = forceOb.compareImg(hdrOb);
  HdrOb::ImgVerdict vsSdr = forceOb.compareImg(sdrOb);
  bool forceHdrOk = forceOb.frames > 30 && vsFollow.diffRatio > 0.5 &&
                    vsSdr.diffRatio < 0.1 && std::abs(vsSdr.midLift) < 15.0;
  std::printf("[mode] forceHDR vs follow midLift=%.1f diffRatio=%.2f | "
              "vs sdr midLift=%.1f diffRatio=%.2f\n",
              vsFollow.midLift, vsFollow.diffRatio, vsSdr.midLift,
              vsSdr.diffRatio);
  bool ok = !g_failed && hdrOk && sdrOk && toneMapOk && metaOk && forceHdrOk;
  std::printf(
      "[AVOX][TEST] case=hdrtest result=%s hdrMean=%.1f sdrMean=%.1f "
      "midLift=%.1f diffRatio=%.2f forceMidLift=%.1f meta=%d/%u%s\n",
      ok ? "PASS" : "FAIL", hdrMean, sdrMean, verdict.midLift,
      verdict.diffRatio, vsSdr.midLift, (int)hdrMetaOb.seen,
      hdrMetaOb.maxLuminance, g_failed ? (" reason: " + g_reason).c_str() : "");
  delete hdrImg;
  delete sdrImg;
  delete forceImg;
  return ok ? 0 : 1;
}
