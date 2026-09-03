#include "MediaPipeFace.hpp"

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
// ===== 常量 (与 face_pipeline.py 逐字一致) =====
constexpr int32_t kDetW = 128;      // BlazeFace 输入
constexpr int32_t kDetH = 128;
constexpr int32_t kLmW = 256;       // landmarker 输入
constexpr int32_t kLmH = 256;
constexpr int32_t kAnchorCount = 896;
constexpr int32_t kArkitBlendshapeCount = 52;
constexpr int32_t kLandmarkCount = 478;
constexpr float kMinScore = 0.5f;   // 检测分数阈值
constexpr float kNmsIou = 0.3f;
constexpr int32_t kMaxDet = 4;

// 478 -> 146 子集 (blendshape 头输入; 逐字移植 face_pipeline.py SUBSET146)
const std::vector<int>& subset146() {
  static const std::vector<int> s = {
      0,1,4,5,6,7,8,10,13,14,17,21,33,37,39,40,46,52,53,54,55,58,61,63,65,66,67,70,78,80,81,82,84,87,88,91,93,95,103,105,107,109,127,132,133,136,144,145,146,148,149,150,152,153,154,155,157,158,159,160,161,162,163,168,172,173,176,178,181,185,191,195,197,234,246,249,251,263,267,269,270,276,282,283,284,285,288,291,293,295,296,297,300,308,310,311,312,314,317,318,321,323,324,332,334,336,338,356,361,362,365,373,374,375,377,378,379,380,381,382,384,385,386,387,388,389,390,397,398,400,402,405,409,415,454,466,468,469,470,471,472,473,474,475,476,477};
  return s;
}

// 1. anchor 生成 (SsdAnchorsCalculator, fixedAnchorSize). strides=[8,16,16,16],
// 每格 2 锚(w=h=1.0). feat=128/stride. -> 896 锚 [cx,cy,w,h] 归一化.
void genAnchors(std::vector<float>& a) {
  const int strides[4] = {8, 16, 16, 16};
  a.clear();
  a.reserve(kAnchorCount * 4);
  for (int st : strides) {
    int feat = kDetW / st;
    for (int y = 0; y < feat; ++y) {
      for (int x = 0; x < feat; ++x) {
        float cx = (x + 0.5f) / feat;
        float cy = (y + 0.5f) / feat;
        // fixedAnchorSize: w=h=1.0; 每格 2 锚(同位)
        a.push_back(cx); a.push_back(cy); a.push_back(1.f); a.push_back(1.f);
        a.push_back(cx); a.push_back(cy); a.push_back(1.f); a.push_back(1.f);
      }
    }
  }
}

// 2. 检测器预处理: letterbox(keepAspect, 零填充) 到 128, RGB, norm[-1,1] NHWC.
// s/padX/padY 供坐标回映. (rgb 已是 RGB 顺序)
void detPreprocess(const cv::Mat& rgb, std::vector<float>& out,
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
  out.resize(kDetW * kDetH * 3);
  const float inv = 1.f / 127.5f;
  int idx = 0;
  for (int y = 0; y < kDetH; ++y) {
    const cv::Vec3b* row = canvas.ptr<cv::Vec3b>(y);
    for (int x = 0; x < kDetW; ++x) {
      out[idx++] = row[x][0] * inv - 1.f;
      out[idx++] = row[x][1] * inv - 1.f;
      out[idx++] = row[x][2] * inv - 1.f;
    }
  }
}
}  // namespace

MediaPipeFace::MediaPipeFace() : chunkQueue(8) {
  taskName = "mediapipe face task";
}

MediaPipeFace::~MediaPipeFace() {
  stop();
}

// ========== VideoFace 接口 ==========

bool MediaPipeFace::loading() { return running(); }

void MediaPipeFace::start() {
  // 幂等: 已在跑不重启; 清上一轮残留 (避免跨段串帧)
  if (!running()) {
    chunkQueue.clear();
    startTask();
  }
}

void MediaPipeFace::feed(IImageBuffer* img, int64_t pts) {
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
  // rowPitch 可能对齐到 16 字节, 按行紧凑拷贝
  int32_t rp = (fmt.rowPitch > 0) ? fmt.rowPitch : W * 4;
  MediaPipeChunk item;
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
  // 队列满时丢老帧 (视觉测试宁可跳帧也不累积延迟)
  chunkQueue.enqueue(item, true);
}

void MediaPipeFace::stop() { stopTask(); }

// ========== RunTask ==========

void MediaPipeFace::onRunTask() {
  // 首次进入: 加载 3 模型 (OnnxSessionCache 共享, 复用不重载)
  if (!initEngine()) {
    LOGFLF(LogLevel::warn, "MediaPipeFace init engine failed (model missing?)");
    dispatch(&IVideoFaceOb::onFaceError, "mediapipe model load failed");
    return;
  }
  VideoFaceDesc fd = {30, kArkitBlendshapeCount, kLandmarkCount};
  dispatch(&IVideoFaceOb::onFaceDesc, fd);
  while (running()) {
    MediaPipeChunk item;
    if (chunkQueue.dequeue(item)) {
      processFrame(item);
    } else {
      sleepTask(false, 5);
    }
  }
  // running()=false: 短排空已入队帧, 避免尾部丢帧
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < deadline) {
    MediaPipeChunk item;
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

bool MediaPipeFace::initEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!bInited) {
    detSession = OnnxModelUser::session(OnnxModel::MediaPipeFaceDetector, false, 0, 4);
    lmSession = OnnxModelUser::session(OnnxModel::MediaPipeFaceLandmarker, false, 0, 4);
    bsSession = OnnxModelUser::session(OnnxModel::MediaPipeBlendshape, false, 0, 4);
    if (!detSession || !detSession->isLoaded() ||
        !lmSession || !lmSession->isLoaded() ||
        !bsSession || !bsSession->isLoaded()) {
      detSession = lmSession = bsSession = nullptr;
      return false;
    }
    detInName = detSession->getInputNames()[0];
    detOutNames = detSession->getOutputNames();
    lmInName = lmSession->getInputNames()[0];
    lmOutNames = lmSession->getOutputNames();
    bsInName = bsSession->getInputNames()[0];
    bsOutNames = bsSession->getOutputNames();
    if (detOutNames.size() < 2 || lmOutNames.size() < 2 || bsOutNames.empty()) {
      return false;
    }
    genAnchors(anchors);
    // 预分配缓冲
    detInput.resize(kDetW * kDetH * 3);
    lmInput.resize(kLmW * kLmH * 3);
    bsInput.resize(subset146().size() * 2);
    lm478.resize(kLandmarkCount * 3);
    imgLandmarks.resize(kLandmarkCount * 2);
    bs52.resize(kArkitBlendshapeCount);
    bInited = true;
  }
  return bInited;
}

bool MediaPipeFace::detectFace(const cv::Mat& rgb, FaceDetection& out) {
  // 2. 预处理
  float s = 1.f;
  int padX = 0, padY = 0;
  detPreprocess(rgb, detInput, s, padX, padY);
  // 推理: regressors[1,896,16] + classificators[1,896,1] (按 size 识别, 防输出序漂移)
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> inputs = {
      {detInName, detInput.data(), {1, kDetH, kDetW, 3}}};
  std::vector<std::vector<float>> outputs;
  if (!detSession->runShaped(inputs, detOutNames, outputs) || outputs.size() < 2) {
    return false;
  }
  const std::vector<float>* regV = nullptr;
  const std::vector<float>* clsV = nullptr;
  for (const auto& o : outputs) {
    if (o.size() == size_t(kAnchorCount * 16)) {
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
  // 解码 + 收候选 (线性 /128; reverseOutputOrder box=[xc,yc,w,h]; 分数 sigmoid clip±88)
  struct Cand {
    int idx;
    float score;
    float x1, y1, x2, y2;  // 归一化 [0,1] (128 letterbox 空间)
  };
  std::vector<Cand> cands;
  for (int i = 0; i < kAnchorCount; ++i) {
    float logit = std::clamp(cls[i], -88.f, 88.f);
    float score = 1.f / (1.f + std::exp(-logit));
    if (score < kMinScore) {
      continue;
    }
    const float* r = reg + i * 16;
    float acx = anchors[i * 4 + 0];
    float acy = anchors[i * 4 + 1];
    float aw = anchors[i * 4 + 2];
    float ah = anchors[i * 4 + 3];
    float xc = r[0] / 128.f * aw + acx;
    float yc = r[1] / 128.f * ah + acy;
    float w = r[2] / 128.f * aw;
    float h = r[3] / 128.f * ah;
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
  // 取最高分 (keep[0] 已是分数最高)
  const Cand& best = cands[keep[0]];
  int i = best.idx;
  const float* r = reg + i * 16;
  float acx = anchors[i * 4 + 0];
  float acy = anchors[i * 4 + 1];
  // 归一化 -> 原图像素: (n*128 - pad)/s
  auto nx2ox = [&](float n) { return (n * kDetW - padX) / s; };
  auto ny2oy = [&](float n) { return (n * kDetH - padY) / s; };
  float xc_n = r[0] / 128.f + acx;
  float yc_n = r[1] / 128.f + acy;
  float w_n = r[2] / 128.f;
  float h_n = r[3] / 128.f;
  out.score = best.score;
  out.cx = nx2ox(xc_n);
  out.cy = ny2oy(yc_n);
  out.w = w_n * kDetW / s;
  out.h = h_n * kDetH / s;
  for (int k = 0; k < 6; ++k) {
    int off = 4 + k * 2;
    float kx_n = r[off] / 128.f + acx;
    float ky_n = r[off + 1] / 128.f + acy;
    out.kps[k][0] = nx2ox(kx_n);
    out.kps[k][1] = ny2oy(ky_n);
  }
  return true;
}

// 3. 对齐仿射 warp 到 256 (dst->src M, 须配 WARP_INVERSE_MAP)
void MediaPipeFace::alignWarp(const cv::Mat& rgb, const FaceDetection& det, cv::Mat& warp) {
  float cx = det.cx, cy = det.cy, w = det.w, h = det.h;
  float rx = det.kps[0][0], ry = det.kps[0][1];  // 右眼
  float lx = det.kps[1][0], ly = det.kps[1][1];  // 左眼
  float theta = std::atan2(ly - ry, lx - rx);     // 眼线倾角
  float S = 1.5f * std::max(w, h);                 // roiScale 1.5 + squareLong
  float c = std::cos(theta), sn = std::sin(theta);
  float k = S / kLmW;                              // = S/256
  // M = R(theta)*(S/256) dst->src; landmark 回投直接 image = M·pts (勿求逆)
  lastM = (cv::Mat_<float>(2, 3) <<
      c * k,  -sn * k, cx - k * (c * 128.f - sn * 128.f),
      sn * k,   c * k, cy - k * (sn * 128.f + c * 128.f));
  // M 是 dst->src 逆映射: 必须加 WARP_INVERSE_MAP, 否则 OpenCV 当正向再求逆 -> 双反 -> 涂抹
  cv::warpAffine(rgb, warp, lastM, cv::Size(kLmW, kLmH),
                 cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
}

void MediaPipeFace::processFrame(const MediaPipeChunk& chunk) {
  if (!detSession || chunk.rgba.empty()) {
    return;
  }
  int32_t W = chunk.width, H = chunk.height;
  // rgba8 -> RGB (模型/对齐全程 RGB 顺序)
  cv::Mat rgba(H, W, CV_8UC4, const_cast<uint8_t*>(chunk.rgba.data()));
  cv::Mat rgb;
  cv::cvtColor(rgba, rgb, cv::COLOR_RGBA2RGB);
  // 检测
  FaceDetection det;
  if (!detectFace(rgb, det)) {
    // 未检出脸: 仍回调 (count=0) 让调用方清空叠加; blendshape hold 上一帧(无则零)
    std::lock_guard<std::mutex> lock(mutex);
    AvoxData rawBs = {reinterpret_cast<uint8_t*>(bs52.data()),
                     kArkitBlendshapeCount * int32_t(sizeof(float)), true};
    dispatch(&IVideoFaceOb::onFaceBlendshape, rawBs, chunk.pts, true);
    AvoxData empty = {nullptr, 0, true};
    dispatch(&IVideoFaceOb::onFaceLandmarks, empty, 0, W, H, chunk.pts);
    return;
  }
  // 对齐 warp
  cv::Mat warp;
  alignWarp(rgb, det, warp);
  // 4. landmarker: RGB norm[-1,1] NHWC -> 478×3 + presence
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
  if (!lmSession->runShaped(lmInputs, lmOutNames, lmOuts) || lmOuts.size() < 2) {
    dispatch(&IVideoFaceOb::onFaceError, "landmarker runShaped failed");
    return;
  }
  // Identity=1434(478×3) + presence=1 (按 size 识别)
  const std::vector<float>* lmV = nullptr;
  const std::vector<float>* presV = nullptr;
  for (const auto& o : lmOuts) {
    if (o.size() == size_t(kLandmarkCount * 3)) {
      lmV = &o;
    } else if (!o.empty()) {
      presV = &o;
    }
  }
  if (!lmV) {
    return;
  }
  memcpy(lm478.data(), lmV->data(), lm478.size() * sizeof(float));
  // 5. blendshape: 146 子集 x,y (warp 空间) -> 52
  const auto& sub = subset146();
  for (size_t i = 0; i < sub.size(); ++i) {
    int si = sub[i];
    bsInput[i * 2 + 0] = lm478[si * 3 + 0];
    bsInput[i * 2 + 1] = lm478[si * 3 + 1];
  }
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> bsInputs = {
      {bsInName, bsInput.data(), {1, int64_t(sub.size()), 2}}};
  std::vector<std::vector<float>> bsOuts;
  if (!bsSession->runShaped(bsInputs, bsOutNames, bsOuts) || bsOuts.empty()) {
    dispatch(&IVideoFaceOb::onFaceError, "blendshape runShaped failed");
    return;
  }
  memcpy(bs52.data(), bsOuts[0].data(), bs52.size() * sizeof(float));
  // landmark 反投回输入帧坐标: pts 在 warp(dst) 空间, M 是 dst->src, image = M·pts
  cv::Mat pts(kLandmarkCount, 1, CV_32FC2);
  for (int i = 0; i < kLandmarkCount; ++i) {
    pts.at<cv::Vec2f>(i, 0)[0] = lm478[i * 3 + 0];
    pts.at<cv::Vec2f>(i, 0)[1] = lm478[i * 3 + 1];
  }
  cv::Mat imgPts;
  cv::transform(pts, imgPts, lastM);
  for (int i = 0; i < kLandmarkCount; ++i) {
    imgLandmarks[i * 2 + 0] = imgPts.at<cv::Vec2f>(i, 0)[0];
    imgLandmarks[i * 2 + 1] = imgPts.at<cv::Vec2f>(i, 0)[1];
  }
  // dispatch (raw 指向成员缓冲, dispatch 同步, 回调内有效)
  std::lock_guard<std::mutex> lock(mutex);
  AvoxData rawBs = {reinterpret_cast<uint8_t*>(bs52.data()),
                   kArkitBlendshapeCount * int32_t(sizeof(float)), true};
  dispatch(&IVideoFaceOb::onFaceBlendshape, rawBs, chunk.pts, true);
  AvoxData rawLm = {reinterpret_cast<uint8_t*>(imgLandmarks.data()),
                   kLandmarkCount * 2 * int32_t(sizeof(float)), true};
  dispatch(&IVideoFaceOb::onFaceLandmarks, rawLm, kLandmarkCount, W, H, chunk.pts);
}

}
