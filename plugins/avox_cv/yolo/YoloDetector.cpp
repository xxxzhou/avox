#include "YoloDetector.hpp"

#include "avox/module/AssetLoader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <regex>

namespace avox {

// IImageBuffer -> BGR(CV_8UC3), 归一各通道序为 BGR (与 TextRecognizer 同款自包含实现)
cv::Mat YoloDetector::bufferToBgr(IImageBuffer* buf) {
  if (!buf) return cv::Mat();
  ImageFormat fmt = buf->getImageFormat();
  if (!fmt.bVailid()) return cv::Mat();
  int ch = 0, code = -1;
  switch (fmt.imageType) {
    case ImageType::bgr8:  ch = 3; code = -1; break;
    case ImageType::rgb8:  ch = 3; code = cv::COLOR_RGB2BGR; break;
    case ImageType::bgra8: ch = 4; code = cv::COLOR_BGRA2BGR; break;
    case ImageType::rgba8: ch = 4; code = cv::COLOR_RGBA2BGR; break;
    case ImageType::r8:    ch = 1; code = cv::COLOR_GRAY2BGR; break;
    default: return cv::Mat();
  }
  int rowBytes = fmt.rowPitch > 0 ? static_cast<int>(fmt.rowPitch) : fmt.width * ch;
  int type = (ch == 1) ? CV_8UC1 : (ch == 3 ? CV_8UC3 : CV_8UC4);
  cv::Mat raw(fmt.height, fmt.width, type, buf->getPointer(), rowBytes);
  cv::Mat bgr;
  if (code < 0) raw.copyTo(bgr);
  else cv::cvtColor(raw, bgr, code);
  return bgr;
}

// 解析 Ultralytics names: "{0: 'person', 1: 'bicycle_x'}" -> ["person","bicycle x"]
void YoloDetector::parseNames(const std::string& raw) {
  names.clear();
  if (raw.empty()) return;
  std::regex re("(\\d+)\\s*:\\s*'([^']*)'");
  std::smatch m;
  std::string s = raw;
  int maxId = -1;
  // 先扫一遍拿最大 id 定长
  std::vector<std::pair<int, std::string>> entries;
  while (std::regex_search(s, m, re)) {
    int id = std::stoi(m[1].str());
    std::string name = m[2].str();
    std::replace(name.begin(), name.end(), '_', ' ');  // '_' -> ' '
    entries.emplace_back(id, std::move(name));
    if (id > maxId) maxId = id;
    s = m.suffix().str();
  }
  if (maxId < 0) return;
  names.assign(maxId + 1, std::string());
  for (auto& e : entries) names[e.first] = std::move(e.second);
}

// letterbox: 等比 min 缩放 + 居中黑底(0)零填充 -> RGB NCHW float (/255)
// 与 detector.py._letterbox 一致: nw=int(w*scale) (截断), pad=(imgsz-nw)//2 (整除)
void YoloDetector::letterbox(const cv::Mat& bgr, std::vector<float>& chw,
                             float& scale, int& padW, int& padH) {
  int h = bgr.rows, w = bgr.cols;
  scale = std::min(static_cast<float>(imgsz) / w, static_cast<float>(imgsz) / h);
  int nw = static_cast<int>(w * scale);
  int nh = static_cast<int>(h * scale);
  padW = (imgsz - nw) / 2;
  padH = (imgsz - nh) / 2;
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  cv::Mat resized;
  if (nw != w || nh != h) cv::resize(rgb, resized, cv::Size(nw, nh));
  else resized = rgb;
  cv::Mat canvas(imgsz, imgsz, CV_8UC3, cv::Scalar(0, 0, 0));
  resized.copyTo(canvas(cv::Rect(padW, padH, nw, nh)));
  // /255 + HWC->CHW
  chw.assign(static_cast<size_t>(3) * imgsz * imgsz, 0.0f);
  size_t plane = static_cast<size_t>(imgsz) * imgsz;
  for (int y = 0; y < imgsz; ++y) {
    const cv::Vec3b* p = canvas.ptr<cv::Vec3b>(y);
    for (int x = 0; x < imgsz; ++x) {
      chw[0 * plane + y * imgsz + x] = p[x][0] / 255.0f;
      chw[1 * plane + y * imgsz + x] = p[x][1] / 255.0f;
      chw[2 * plane + y * imgsz + x] = p[x][2] / 255.0f;
    }
  }
}

// 精确 resize (拉伸到 imgsz×imgsz, 无 padding) -> RGB NCHW float (/255), classify 用
void YoloDetector::exactResize(const cv::Mat& bgr, std::vector<float>& chw) {
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(imgsz, imgsz));
  chw.assign(static_cast<size_t>(3) * imgsz * imgsz, 0.0f);
  size_t plane = static_cast<size_t>(imgsz) * imgsz;
  for (int y = 0; y < imgsz; ++y) {
    const cv::Vec3b* p = resized.ptr<cv::Vec3b>(y);
    for (int x = 0; x < imgsz; ++x) {
      chw[0 * plane + y * imgsz + x] = p[x][0] / 255.0f;
      chw[1 * plane + y * imgsz + x] = p[x][1] / 255.0f;
      chw[2 * plane + y * imgsz + x] = p[x][2] / 255.0f;
    }
  }
}

// 两 xyxy 框 IoU
static float boxIou(const YoloBox& a, const YoloBox& b) {
  float ix1 = std::max(a.x1, b.x1);
  float iy1 = std::max(a.y1, b.y1);
  float ix2 = std::min(a.x2, b.x2);
  float iy2 = std::min(a.y2, b.y2);
  float iw = std::max(0.0f, ix2 - ix1);
  float ih = std::max(0.0f, iy2 - iy1);
  float inter = iw * ih;
  float areaA = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
  float areaB = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
  float uni = areaA + areaB - inter;
  return uni > 0 ? inter / uni : 0.0f;
}

// 检测后处理: out 布局 [1, channels, N] (channels=4+nc); 写入 detections (按类贪心纯 IoU NMS)
void YoloDetector::decodeDetect(const std::vector<float>& out, int channels, int numAnchors,
                                int origW, int origH, float scale, int padW, int padH,
                                float confThresh, float iouThresh) {
  detections.clear();
  if (channels <= 4 || numAnchors <= 0) return;
  int nc = channels - 4;
  // 候选 (conf 过滤后)
  std::vector<YoloBox> cand;
  cand.reserve(numAnchors);
  for (int n = 0; n < numAnchors; ++n) {
    float cx = out[0 * numAnchors + n];
    float cy = out[1 * numAnchors + n];
    float w = out[2 * numAnchors + n];
    float h = out[3 * numAnchors + n];
    int bestCls = 0;
    float bestConf = out[4 * numAnchors + n];
    for (int c = 5; c < channels; ++c) {
      float v = out[c * numAnchors + n];
      if (v > bestConf) { bestConf = v; bestCls = c - 4; }
    }
    if (bestConf <= confThresh) continue;  // 严格 > (detector.py: confs > conf_t)
    // xywh -> xyxy -> 反 letterbox -> floor
    float x1 = (cx - w * 0.5f - padW) / scale;
    float y1 = (cy - h * 0.5f - padH) / scale;
    float x2 = (cx + w * 0.5f - padW) / scale;
    float y2 = (cy + h * 0.5f - padH) / scale;
    YoloBox b;
    b.x1 = std::floor(x1);
    b.y1 = std::floor(y1);
    b.x2 = std::floor(x2);
    b.y2 = std::floor(y2);
    b.score = bestConf;
    b.classId = bestCls;
    if (bestCls < nc) cand.push_back(b);
  }
  if (cand.empty()) return;
  // 按分数降序
  std::sort(cand.begin(), cand.end(), [](const YoloBox& a, const YoloBox& b) {
    return a.score > b.score;
  });
  // 按类贪心纯 IoU NMS
  std::vector<char> suppressed(cand.size(), 0);
  for (size_t i = 0; i < cand.size(); ++i) {
    if (suppressed[i]) continue;
    detections.push_back(cand[i]);
    for (size_t j = 0; j < cand.size(); ++j) {
      if (j == i || suppressed[j]) continue;
      if (cand[j].classId != cand[i].classId) continue;  // 不同类不互相抑制
      if (boxIou(cand[i], cand[j]) > iouThresh) suppressed[j] = 1;
    }
  }
}

bool YoloDetector::ensureLoaded() {
  if (session) return true;
  if (modelPath.empty()) {
    lastError = "ensureLoaded: modelPath 未设置";
    return false;
  }
  session = OnnxModelUser::session(modelPath, useGpu);
  if (!session || !session->isLoaded()) {
    session = nullptr;
    lastError = "ensureLoaded: 加载模型失败: " + modelPath;
    return false;
  }
  // in/out 名 (取第 0 个)
  auto ins = session->getInputNames();
  auto outs = session->getOutputNames();
  if (ins.empty() || outs.empty()) {
    lastError = "ensureLoaded: 模型无 input/output";
    return false;
  }
  inName = ins[0];
  outName = outs[0];
  // 元数据: task / names / imgsz
  auto meta = session->getCustomMetadata();
  auto it = meta.find("task");
  task = (it != meta.end() && !it->second.empty()) ? it->second : std::string("detect");
  // names
  auto nit = meta.find("names");
  if (nit != meta.end()) parseNames(nit->second);
  numClasses = static_cast<int>(names.size());
  // imgsz: 优先输入 shape [1,3,H,W] 的 H, 回退元数据, 默认 640
  imgsz = 640;
  auto ishape = session->getInputShape(inName);
  if (ishape.size() >= 4 && ishape[2] > 0) {
    imgsz = static_cast<int>(ishape[2]);
  } else {
    auto mit = meta.find("imgsz");
    if (mit != meta.end()) {
      // 可能是 "(640, 640)" / "[640, 640]" / "640"
      std::regex numre("(\\d+)");
      std::smatch m;
      std::string s = mit->second;
      if (std::regex_search(s, m, numre)) imgsz = std::stoi(m[1].str());
    }
  }
  // 无 names 时按输出 shape 推 nc (detect: channels-4)
  if (numClasses == 0) {
    auto oshape = session->getOutputShape(outName);
    if (oshape.size() >= 2 && oshape[1] > 4) numClasses = static_cast<int>(oshape[1]) - 4;
  }
  lastError.clear();
  return true;
}

void YoloDetector::setModelPath(const char* path) {
  std::string p = path ? path : "";
  // 智能路径: 绝对路径 (D:/, /) 原样用; 相对路径按 assets 模型根解析为 models/<p>
  // (与 onnxModelPath 同约定; ONNXSession::loadModel 再经 AssetLoader 在 assets/models 下找到)
  std::string resolved = p;
  if (!p.empty() && AssetLoader::isAssetPath(p)) {
    resolved = "models/" + p;
  }
  if (resolved != modelPath) {
    modelPath = resolved;
    session = nullptr;  // 路径变了, 下次 ensureLoaded 重新加载
  }
}

const char* YoloDetector::getTask() {
  ensureLoaded();
  return task.c_str();
}

int32_t YoloDetector::getClassCount() {
  ensureLoaded();
  return numClasses;
}

const char* YoloDetector::getClassName(int32_t classId) {
  ensureLoaded();
  if (classId >= 0 && classId < static_cast<int32_t>(names.size())) return names[classId].c_str();
  return "";
}

int32_t YoloDetector::detect(IImageBuffer* scene) {
  auto start = std::chrono::steady_clock::now();
  detections.clear();
  matchTimeMs = 0.0f;
  if (!ensureLoaded()) return 0;
  if (task == "classify") {
    lastError = "detect: 模型 task=classify, 请用 classify()";
    return 0;
  }
  cv::Mat bgr = bufferToBgr(scene);
  if (bgr.empty()) {
    lastError = "detect: scene 格式不支持";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return 0;
  }
  int origW = bgr.cols, origH = bgr.rows;
  // letterbox 预处理
  std::vector<float> input;
  float scale = 1.0f;
  int padW = 0, padH = 0;
  letterbox(bgr, input, scale, padW, padH);
  // 推理 (输入 H/W 可能符号化, 用 runShaped)
  std::vector<int64_t> shape = {1, 3, imgsz, imgsz};
  std::vector<std::vector<float>> outputs;
  if (!session->runShaped({{inName, input.data(), shape}}, {outName}, outputs) || outputs.empty()) {
    lastError = "detect: 推理失败";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return 0;
  }
  // 输出 [1, channels, N]; channels=4+nc (无 names 时从 shape 推)
  size_t total = outputs[0].size();
  int channels = 4 + numClasses;
  if (channels <= 4) {
    auto oshape = session->getOutputShape(outName);
    if (oshape.size() >= 2 && oshape[1] > 0) channels = static_cast<int>(oshape[1]);
  }
  int numAnchors = channels > 0 ? static_cast<int>(total / channels) : 0;
  decodeDetect(outputs[0], channels, numAnchors, origW, origH, scale, padW, padH,
               confThreshold, iouThreshold);
  lastError.clear();
  matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
  return static_cast<int32_t>(detections.size());
}

int32_t YoloDetector::getDetectionCount() {
  return static_cast<int32_t>(detections.size());
}

bool YoloDetector::getDetection(int32_t index, YoloBox* out) {
  if (!out || index < 0 || index >= static_cast<int32_t>(detections.size())) return false;
  *out = detections[index];
  return true;
}

int32_t YoloDetector::classify(IImageBuffer* scene) {
  auto start = std::chrono::steady_clock::now();
  classifyScore = 0.0f;
  matchTimeMs = 0.0f;
  if (!ensureLoaded()) return -1;
  if (task == "detect") {
    lastError = "classify: 模型 task=detect, 请用 detect()";
    return -1;
  }
  cv::Mat bgr = bufferToBgr(scene);
  if (bgr.empty()) {
    lastError = "classify: scene 格式不支持";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return -1;
  }
  // 精确 resize (非 letterbox) 预处理
  std::vector<float> input;
  exactResize(bgr, input);
  std::vector<int64_t> shape = {1, 3, imgsz, imgsz};
  std::vector<std::vector<float>> outputs;
  if (!session->runShaped({{inName, input.data(), shape}}, {outName}, outputs) || outputs.empty()) {
    lastError = "classify: 推理失败";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return -1;
  }
  // 输出 [1, nc] flatten -> argmax (图已含 sigmoid/softmax, 取原值)
  const std::vector<float>& o = outputs[0];
  if (o.empty()) {
    lastError = "classify: 空输出";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return -1;
  }
  int top = 0;
  for (int i = 1; i < static_cast<int>(o.size()); ++i) {
    if (o[i] > o[top]) top = i;
  }
  classifyScore = o[top];
  lastError.clear();
  matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
  return top;
}

}
