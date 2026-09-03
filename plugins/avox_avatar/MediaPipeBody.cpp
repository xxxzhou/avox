#include "MediaPipeBody.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>

#include <opencv2/imgproc.hpp>

#include "avox/Avox.hpp"      // timeStampMS / LOGFLF
#include "avox/AvoxVideo.h"   // IImageBuffer / ImageFormat

namespace avox {

namespace {
// ===== 常量 (与 MediaPipe BlazePose 配置一致) =====
constexpr int32_t kDetW = 224;      // BlazePose 检测器输入 (NCHW 224x224)
constexpr int32_t kDetH = 224;
constexpr int32_t kLmW = 256;       // pose_landmarker 输入 (NHWC 256x256)
constexpr int32_t kLmH = 256;
constexpr int32_t kAnchorCount = 2254;  // 5 层 (strides 8/16/32/32/32) 每格 2 锚
constexpr int32_t kBodyLandmarkCount = 33;
constexpr float kMinScore = 0.1f;   // BlazePose sigmoid 分数天然偏低 (~0.2~0.3), 阈值需远低于 0.5
constexpr float kNmsIou = 0.3f;
constexpr int32_t kMaxDet = 4;

// BlazePose 检测器锚生成: strides=[8,16,32,32,32], 每格 2 锚 (fixed_anchor_size, w=h=1.0)。
// 与 MediaPipe SsdAnchorsCalculator 对齐 (input 224; 每格 anchors_per_location=2)。
void genBodyAnchors(std::vector<float>& a) {
  const int strides[5] = {8, 16, 32, 32, 32};
  a.clear();
  a.reserve(kAnchorCount * 4);
  for (int st : strides) {
    int feat = kDetW / st;
    for (int y = 0; y < feat; ++y) {
      for (int x = 0; x < feat; ++x) {
        float cx = (x + 0.5f) / feat;
        float cy = (y + 0.5f) / feat;
        a.push_back(cx); a.push_back(cy); a.push_back(1.f); a.push_back(1.f);
        a.push_back(cx); a.push_back(cy); a.push_back(1.f); a.push_back(1.f);
      }
    }
  }
}

// BlazePose 检测器预处理: resize 到 224 (keepAspect, 零填充), RGB norm[-1,1] NCHW.
// 返回 scale/pad 供坐标回映.
void detBodyPreprocess(const cv::Mat& rgb, std::vector<float>& out,
                       float& s, int& padX, int& padY) {
  int W0 = rgb.cols, H0 = rgb.rows;
  s = std::min(kDetW / float(W0), kDetH / float(H0));
  int nw = int(std::round(W0 * s));
  int nh = int(std::round(H0 * s));
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(nw, nh));
  cv::Mat canvas = cv::Mat::zeros(kDetH, kDetW, CV_8UC3);
  padX = (kDetW - nw) / 2;
  padY = (kDetH - nh) / 2;
  cv::Mat roi(canvas, cv::Rect(padX, padY, nw, nh));
  resized.copyTo(roi);
  out.resize(3 * kDetH * kDetW);  // NCHW
  const float inv = 1.f / 127.5f;
  int idx = 0;
  for (int c = 0; c < 3; ++c) {
    for (int y = 0; y < kDetH; ++y) {
      const cv::Vec3b* row = canvas.ptr<cv::Vec3b>(y);
      for (int x = 0; x < kDetW; ++x) {
        out[idx++] = row[x][c] * inv - 1.f;
      }
    }
  }
}
}  // namespace

MediaPipeBody::MediaPipeBody() : chunkQueue(8) {
  taskName = "mediapipe body task";
}

MediaPipeBody::~MediaPipeBody() {
  stop();
}

// ========== BodyImpl 接口 ==========

bool MediaPipeBody::loading() { return running(); }

void MediaPipeBody::start() {
  if (!running()) {
    chunkQueue.clear();
    startTask();
  }
}

void MediaPipeBody::feed(IImageBuffer* img, int64_t pts) {
  if (!running()) {
    start();
  }
  if (!img) {
    return;
  }
  ImageFormat fmt = img->getImageFormat();
  int32_t W = fmt.width, H = fmt.height;
  if (W <= 0 || H <= 0) {
    return;
  }
  uint8_t* ptr = img->getPointer();
  if (!ptr) {
    return;
  }
  int32_t rp = (fmt.rowPitch > 0) ? fmt.rowPitch : W * 4;
  BodyChunk item;
  item.width = W;
  item.height = H;
  item.pts = (pts == 0) ? timeStampMS() : pts;
  item.rgba.resize(size_t(W) * H * 4);
  if (rp == W * 4) {
    memcpy(item.rgba.data(), ptr, size_t(W) * H * 4);
  } else {
    for (int32_t y = 0; y < H; ++y) {
      memcpy(item.rgba.data() + size_t(y) * W * 4, ptr + size_t(y) * rp, size_t(W) * 4);
    }
  }
  chunkQueue.enqueue(item, true);  // 队列满丢老帧
}

void MediaPipeBody::stop() { stopTask(); }

// ========== RunTask ==========

void MediaPipeBody::onRunTask() {
  if (!initEngine()) {
    LOGFLF(LogLevel::warn, "MediaPipeBody init engine failed (model missing?)");
    dispatch(&IBodyOb::onBodyError, "mediapipe body model load failed");
    return;
  }
  BodyDesc fd = {30, kBodyLandmarkCount};
  dispatch(&IBodyOb::onBodyDesc, fd);
  while (running()) {
    BodyChunk item;
    if (chunkQueue.dequeue(item)) {
      processFrame(item);
    } else {
      sleepTask(false, 5);
    }
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < deadline) {
    BodyChunk item;
    if (!chunkQueue.dequeue(item)) {
      sleepTask(false, 10);
      if (!chunkQueue.dequeue(item)) {
        break;
      }
    }
    processFrame(item);
  }
  chunkQueue.clear();
}

// ========== 内部方法 ==========

bool MediaPipeBody::initEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!bInited) {
    detSession = OnnxModelUser::session(OnnxModel::MediaPipePoseDetector, false, 0, 4);
    lmSession = OnnxModelUser::session(OnnxModel::MediaPipePoseLandmarker, false, 0, 4);
    if (!detSession || !detSession->isLoaded() ||
        !lmSession || !lmSession->isLoaded()) {
      detSession = lmSession = nullptr;
      return false;
    }
    detInName = detSession->getInputNames()[0];
    detOutNames = detSession->getOutputNames();
    lmInName = lmSession->getInputNames()[0];
    lmOutNames = lmSession->getOutputNames();
    if (detOutNames.size() < 2 || lmOutNames.empty()) {
      return false;
    }
    genBodyAnchors(anchors);
    detInput.resize(3 * kDetH * kDetW);
    lmInput.resize(kLmW * kLmH * 3);
    body33.resize(kBodyLandmarkCount * 4);  // x,y,z + conf (热图 argmax 峰值)
    bInited = true;
  }
  return bInited;
}

bool MediaPipeBody::detectBody(const cv::Mat& rgb, BodyDetection& out) {
  float s = 1.f;
  int padX = 0, padY = 0;
  detBodyPreprocess(rgb, detInput, s, padX, padY);
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> inputs = {
      {detInName, detInput.data(), {1, 3, kDetH, kDetW}}};
  std::vector<std::vector<float>> outputs;
  if (!detSession->runShaped(inputs, detOutNames, outputs) || outputs.size() < 2) {
    return false;
  }
  // 按 size 识别: Identity[1,2254,12] (box4+4kp) + Identity_1[1,2254,1] 分数
  const std::vector<float>* regV = nullptr;
  const std::vector<float>* clsV = nullptr;
  for (const auto& o : outputs) {
    if (o.size() == size_t(kAnchorCount * 12)) {
      regV = &o;
    } else if (o.size() == size_t(kAnchorCount)) {
      clsV = &o;
    }
  }
  if (!regV || !clsV) {
    return false;
  }
  const float* reg = regV->data();
  const float* cls = clsV->data();
  // 解码 + 收候选 (线性 /224, 与 face_detector 同范式: face 用 /128, pose 输入 224 故 /224)
  // BlazePose sigmoid 分数天然偏低 (~0.2), 阈值 0.1 (见 kMinScore).
  struct Cand {
    int idx;
    float score;
    float x1, y1, x2, y2;  // 归一化 [0,1] (224 letterbox 空间)
  };
  std::vector<Cand> cands;
  for (int i = 0; i < kAnchorCount; ++i) {
    float logit = std::clamp(cls[i], -88.f, 88.f);
    float score = 1.f / (1.f + std::exp(-logit));
    if (score < kMinScore) {
      continue;
    }
    const float* r = reg + i * 12;
    float acx = anchors[i * 4 + 0];
    float acy = anchors[i * 4 + 1];
    // 线性解码: xc,yc = r/224 + anchor; w,h = r/224 (归一化)
    float xc = r[0] / 224.f + acx;
    float yc = r[1] / 224.f + acy;
    float w = std::fabs(r[2] / 224.f);
    float h = std::fabs(r[3] / 224.f);
    if (w <= 0.f || h <= 0.f) {
      continue;
    }
    cands.push_back({i, score, xc - w / 2, yc - h / 2, xc + w / 2, yc + h / 2});
  }
  if (cands.empty()) {
    return false;
  }
  // NMS (分数降序贪心)
  std::vector<int> order(cands.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return cands[a].score > cands[b].score; });
  std::vector<char> suppressed(cands.size(), 0);
  std::vector<int> keep;
  for (int ii : order) {
    if (suppressed[ii]) {
      continue;
    }
    keep.push_back(ii);
    if (int(keep.size()) >= kMaxDet) {
      break;
    }
    const Cand& A = cands[ii];
    for (int jj : order) {
      if (jj == ii || suppressed[jj]) {
        continue;
      }
      const Cand& B = cands[jj];
      float xx1 = std::max(A.x1, B.x1), yy1 = std::max(A.y1, B.y1);
      float xx2 = std::min(A.x2, B.x2), yy2 = std::min(A.y2, B.y2);
      float iw = std::max(0.f, xx2 - xx1), ih = std::max(0.f, yy2 - yy1);
      float inter = iw * ih;
      float ua = (A.x2 - A.x1) * (A.y2 - A.y1) + (B.x2 - B.x1) * (B.y2 - B.y1) - inter;
      if (inter / std::max(ua, 1e-9f) > kNmsIou) {
        suppressed[jj] = 1;
      }
    }
  }
  // 取最高分
  const Cand& best = cands[keep[0]];
  int i = best.idx;
  const float* r = reg + i * 12;
  float acx = anchors[i * 4 + 0];
  float acy = anchors[i * 4 + 1];
  // 归一化 -> 原图像素: (n*224 - pad)/s (与 face 同范式, 线性 /224; n 为归一化 [0,1])
  auto nx2ox = [&](float n) { return (n * kDetW - padX) / s; };
  auto ny2oy = [&](float n) { return (n * kDetH - padY) / s; };
  // box 线性 /224 (xc,yc 加锚; w,h 取 abs 后 *224 得像素)
  float xc_n = r[0] / 224.f + acx;
  float yc_n = r[1] / 224.f + acy;
  float w_n = std::fabs(r[2] / 224.f);
  float h_n = std::fabs(r[3] / 224.f);
  out.score = best.score;
  out.cx = nx2ox(xc_n);
  out.cy = ny2oy(yc_n);
  out.w = w_n * kDetW / s;   // 归一化宽 -> 原图像素
  out.h = h_n * kDetH / s;   // 归一化高 -> 原图像素
  // 4 关键点 线性 /224 (与 box 同范式)
  for (int k = 0; k < 4; ++k) {
    float kx_n = r[4 + k * 2] / 224.f + acx;
    float ky_n = r[5 + k * 2] / 224.f + acy;
    out.kps[k][0] = nx2ox(kx_n);
    out.kps[k][1] = ny2oy(ky_n);
  }
  return true;
}

// 2. 人体旋转包围盒对齐 warp 到 256: 用 髋部中心(kp0=mid_hip) 与 肩部中心(kp3=mid_shoulder)
//    定旋转与尺度 (MediaPipe BlazePose 标准), roiSize = 髋肩距离×2.5 覆盖全身。
//    kp 索引: kp0=mid_hip kp1=nose kp2=upper_body_center kp3=mid_shoulder
//    (务必 kp3 作肩, 不是 kp2! 实测 kp2 是上身中心, 用之会偏位)。
void MediaPipeBody::alignBodyWarp(const cv::Mat& rgb, const BodyDetection& det, cv::Mat& warp) {
  float hx = det.kps[0][0], hy = det.kps[0][1];  // kp0 = mid_hip 髋部中点
  float sx = det.kps[3][0], sy = det.kps[3][1];  // kp3 = mid_shoulder 肩部中点
  // 若关键点无效 (未检出), 退回框中心
  if (!(hx > 0 && hy > 0 && sx > 0 && sy > 0)) {
    hx = det.cx;
    hy = det.cy;
    sx = det.cx;
    sy = det.cy - det.h * 0.4f;
  }
  float theta = std::atan2(sx - hx, -(sy - hy));  // 体轴倾角 (绕图像 z)
  float bodyLen = std::sqrt((sx - hx) * (sx - hx) + (sy - hy) * (sy - hy));
  // roiSize: 髋→肩距离×2.5 (MediaPipe 标准, 覆盖全身)
  float roiSize = std::max(bodyLen * 2.5f, std::max(det.w, det.h) * 1.5f);
  if (roiSize <= 0.f) {
    roiSize = std::max(det.w, det.h);
  }
  float c = std::cos(theta), sn = std::sin(theta);
  float k = roiSize / kLmW;
  float cx = (hx + sx) * 0.5f;   // 中心取髋肩中点
  float cy = (hy + sy) * 0.5f;
  // M = R(theta)*(roiSize/256) dst->src; 配 WARP_INVERSE_MAP
  lastM = (cv::Mat_<float>(2, 3) <<
      c * k,  -sn * k, cx - k * (c * 128.f - sn * 128.f),
      sn * k,   c * k, cy - k * (sn * 128.f + c * 128.f));
  cv::warpAffine(rgb, warp, lastM, cv::Size(kLmW, kLmH),
                 cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
}

void MediaPipeBody::processFrame(const BodyChunk& chunk) {
  if (!detSession || chunk.rgba.empty()) {
    return;
  }
  int32_t W = chunk.width, H = chunk.height;
  cv::Mat rgba(H, W, CV_8UC4, const_cast<uint8_t*>(chunk.rgba.data()));
  cv::Mat rgb;
  cv::cvtColor(rgba, rgb, cv::COLOR_RGBA2RGB);
  BodyDetection det;
  if (!detectBody(rgb, det)) {
    // 未检出人: 仍回调 (count=0) 让调用方清空驱动
    std::lock_guard<std::mutex> lock(mutex);
    AvoxData empty = {nullptr, 0, true};
    dispatch(&IBodyOb::onBodyLandmarks, empty, 0, W, H, chunk.pts);
    return;
  }
  // 对齐 warp
  cv::Mat warp;
  alignBodyWarp(rgb, det, warp);
  // 3. pose_landmarker: RGB norm[-1,1] NHWC -> Identity[1,195](前33x3) + presence
  int idx = 0;
  const float inv = 1.f / 127.5f;
  for (int y = 0; y < kLmH; ++y) {
    const cv::Vec3b* row = warp.ptr<cv::Vec3b>(y);
    for (int x = 0; x < kLmW; ++x) {
      lmInput[idx++] = row[x][0] * inv - 1.f;
      lmInput[idx++] = row[x][1] * inv - 1.f;
      lmInput[idx++] = row[x][2] * inv - 1.f;
    }
  }
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> lmInputs = {
      {lmInName, lmInput.data(), {1, kLmH, kLmW, 3}}};
  std::vector<std::vector<float>> lmOuts;
  if (!lmSession->runShaped(lmInputs, lmOutNames, lmOuts) || lmOuts.empty()) {
    dispatch(&IBodyOb::onBodyError, "pose landmarker runShaped failed");
    return;
  }
  // 取 33 点位置: 从 Identity_3 热图 [1,64,64,39] (前 33 通道是 33 个关键点的 64x64 热图)
  // argmax 得 warp(dst) 空间像素位置 (64x64 -> 256x256, x4 + 偏移)。
  // (经 Python 验证 Identity[1,195] 的 195 维不是直接坐标, 用热图才贴合人体)
  const std::vector<float>* hmV = nullptr;
  for (const auto& o : lmOuts) {
    if (o.size() == size_t(64 * 64 * 39)) {
      hmV = &o;
      break;
    }
  }
  if (!hmV) {
    return;
  }
  // argmax 33 通道: 热图布局为 [1,64,64,39] (NHWC, 通道在最后一维)。
  // 第 i 个空间位置 (0..4095) 的第 k 通道值 = hm[i*39 + k]; 每通道 argmax 得 (hy,hx), ×4 转 256 空间像素。
  // 该通道置信度 = spatial softmax 后峰值 (MediaPipe 官方即如此估可信度):
  //   热图集中(模型确信该点) → 峰值接近 1; 平坦(遮挡/模型外推) → 峰值接近 1/4096。
  //   (Identity_3 是 logits 特征, 非概率, 直接取 argmax 峰值不可用 — 可为负/无界)
  cv::Mat pts(kBodyLandmarkCount, 1, CV_32FC2);
  std::vector<float> conf33(kBodyLandmarkCount);  // softmax 后热图峰值置信度 (0,1]
  hmBuf.resize(kBodyLandmarkCount * 2);
  for (int k = 0; k < kBodyLandmarkCount; ++k) {
    int bestIdx = 0;
    float bestVal = -1e30f;
    for (int i = 0; i < 64 * 64; ++i) {
      float v = (*hmV)[i * 39 + k];  // NHWC: 空间在前, 通道在最后
      if (v > bestVal) {
        bestVal = v;
        bestIdx = i;
      }
    }
    // spatial softmax (减最大值防溢出): conf = exp(0) / sum(exp(v-bestVal))
    double sumExp = 0.0;
    for (int i = 0; i < 64 * 64; ++i)
      sumExp += std::exp(double((*hmV)[i * 39 + k]) - bestVal);
    conf33[k] = float(1.0 / sumExp);
    int hy = bestIdx / 64;
    int hx = bestIdx % 64;
    // 64x64 热图 -> 256x256 warp 像素 (×4, 中心加 2)
    pts.at<cv::Vec2f>(k, 0)[0] = hx * 4.f + 2.f;
    pts.at<cv::Vec2f>(k, 0)[1] = hy * 4.f + 2.f;
    hmBuf[k * 2 + 0] = pts.at<cv::Vec2f>(k, 0)[0];
    hmBuf[k * 2 + 1] = pts.at<cv::Vec2f>(k, 0)[1];
  }
  // 用 lastM (dst->src) 反投回输入帧坐标
  cv::Mat imgPts;
  cv::transform(pts, imgPts, lastM);
  for (int i = 0; i < kBodyLandmarkCount; ++i) {
    body33[i * 4 + 0] = imgPts.at<cv::Vec2f>(i, 0)[0];
    body33[i * 4 + 1] = imgPts.at<cv::Vec2f>(i, 0)[1];
    body33[i * 4 + 2] = 0.f;  // 热图无 z; 设为 0 (retargeting 主要用 x,y; z 后续可从 Identity_4 取)
    body33[i * 4 + 3] = conf33[i];
  }
  // dispatch (raw 指向成员缓冲, dispatch 同步, 回调内有效)
  std::lock_guard<std::mutex> lock(mutex);
  AvoxData raw = {reinterpret_cast<uint8_t*>(body33.data()),
                 kBodyLandmarkCount * 4 * int32_t(sizeof(float)), true};
  dispatch(&IBodyOb::onBodyLandmarks, raw, kBodyLandmarkCount, W, H, chunk.pts);
}

}
