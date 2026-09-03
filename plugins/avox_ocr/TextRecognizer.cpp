#include "TextRecognizer.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/Avox.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace avox {

namespace {
// PP-OCR det 归一化 (ImageNet); rec 用 0.5/0.5 (代码内联)
const float kDetMean[3] = {0.485f, 0.456f, 0.406f};
const float kDetStd[3] = {0.229f, 0.224f, 0.225f};
}  // namespace

TextRecognizer::TextRecognizer() {}
TextRecognizer::~TextRecognizer() = default;

void TextRecognizer::setRoi(int32_t x, int32_t y, int32_t w, int32_t h) {
  roi = cv::Rect(x, y, w, h);
  useRoi = (w > 0 && h > 0);
}

bool TextRecognizer::ensureLoaded() {
  if (detSession && recSession) return true;  // 幂等: 已加载直接返回
  lastError.clear();
  const char* kDictName = "ocr/ppocr_keys_v1.txt";
  // det/rec: 经 OnnxModelUser 从全局缓存取 (首次加载, 之后复用; 不 unload 则常驻)
  detSession = session(OnnxModel::OcrDet, useGpu);
  if (!detSession) {
    lastError = "ensureLoaded:det 模型加载失败";
    return false;
  }
  recSession = session(OnnxModel::OcrRec, useGpu);
  if (!recSession) {
    lastError = "ensureLoaded:rec 模型加载失败";
    return false;
  }
  // det 输入/输出名
  auto detIns = detSession->getInputNames();
  auto detOuts = detSession->getOutputNames();
  if (detIns.empty() || detOuts.empty()) {
    lastError = "ensureLoaded:det 模型无输入/输出";
    return false;
  }
  detInName = detIns[0];
  detOutName = detOuts[0];
  // rec 输入/输出名 + 高 + 类别数
  auto recIns = recSession->getInputNames();
  auto recOuts = recSession->getOutputNames();
  if (recIns.empty() || recOuts.empty()) {
    lastError = "ensureLoaded:rec 模型无输入/输出";
    return false;
  }
  recInName = recIns[0];
  recOutName = recOuts[0];
  auto recInShape = recSession->getInputShape(recInName);
  if (recInShape.size() >= 3) recHeight = static_cast<int>(recInShape[recInShape.size() - 2]);
  if (recHeight <= 0) recHeight = 48;
  auto recOutShape = recSession->getOutputShape(recOutName);
  recOutC = (!recOutShape.empty() && recOutShape.back() > 0) ? static_cast<int>(recOutShape.back()) : 0;
  // 字典 (getModelFilePath 返回绝对路径，直接读文件)
  std::string dictPath = getModelFilePath(kDictName);
  std::ifstream dictFile(dictPath, std::ios::binary | std::ios::ate);
  std::vector<uint8_t> dictData;
  if (dictFile.is_open()) {
    auto sz = dictFile.tellg();
    dictFile.seekg(0, std::ios::beg);
    dictData.resize(sz);
    dictFile.read(reinterpret_cast<char*>(dictData.data()), sz);
  }
  if (dictData.empty()) {
    lastError = "ensureLoaded:字典加载失败";
    return false;
  }
  dict.clear();
  std::string content(reinterpret_cast<const char*>(dictData.data()), dictData.size());
  std::istringstream iss(content);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    dict.push_back(line);
  }
  if (dict.empty()) {
    lastError = "ensureLoaded:字典为空";
    return false;
  }
  return true;
}

cv::Mat TextRecognizer::bufferToBgr(IImageBuffer* buf) {
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

bool TextRecognizer::detPreprocess(const cv::Mat& bgr, std::vector<float>& chw, int& inH, int& inW) {
  const int maxSide = 960;
  int h = bgr.rows, w = bgr.cols;
  int m = std::max(h, w);
  float scale = (m > maxSide) ? static_cast<float>(maxSide) / m : 1.0f;
  int rh = std::max(32, static_cast<int>(h * scale));
  int rw = std::max(32, static_cast<int>(w * scale));
  rh = (rh + 31) / 32 * 32;
  rw = (rw + 31) / 32 * 32;
  inH = rh;
  inW = rw;
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(rw, rh));
  resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);
  chw.assign(static_cast<size_t>(3) * rh * rw, 0.0f);
  size_t plane = static_cast<size_t>(rh) * rw;
  for (int y = 0; y < rh; ++y) {
    const cv::Vec3f* p = resized.ptr<cv::Vec3f>(y);
    for (int x = 0; x < rw; ++x) {
      chw[0 * plane + y * rw + x] = (p[x][0] - kDetMean[0]) / kDetStd[0];
      chw[1 * plane + y * rw + x] = (p[x][1] - kDetMean[1]) / kDetStd[1];
      chw[2 * plane + y * rw + x] = (p[x][2] - kDetMean[2]) / kDetStd[2];
    }
  }
  return true;
}

std::vector<cv::Rect> TextRecognizer::detPostprocess(
    const std::vector<float>& probMap, int ph, int pw, int srcH, int srcW) {
  std::vector<cv::Rect> boxes;
  if (ph <= 0 || pw <= 0 || probMap.empty()) return boxes;
  cv::Mat prob(ph, pw, CV_32F, const_cast<float*>(probMap.data()));
  cv::Mat probFull;
  cv::resize(prob, probFull, cv::Size(srcW, srcH), 0, 0, cv::INTER_LINEAR);
  cv::Mat bin = probFull > threshold;
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(bin, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
  cv::Rect bounds(0, 0, srcW, srcH);
  for (const auto& c : contours) {
    if (cv::contourArea(c) < 10.0) continue;
    cv::Rect rect = cv::boundingRect(c);
    int padW = std::max(1, rect.width / 10);
    int padH = std::max(1, rect.height / 10);
    rect.x -= padW;
    rect.y -= padH;
    rect.width += padW * 2;
    rect.height += padH * 2;
    rect &= bounds;
    if (rect.width <= 2 || rect.height <= 2) continue;
    boxes.push_back(rect);
  }
  return boxes;
}

bool TextRecognizer::recPreprocess(const cv::Mat& bgr, const cv::Rect& box,
                                    std::vector<float>& chw, int& inW) {
  cv::Rect r = box & cv::Rect(0, 0, bgr.cols, bgr.rows);
  if (r.width <= 0 || r.height <= 0) return false;
  cv::Mat crop = bgr(r).clone();
  cv::cvtColor(crop, crop, cv::COLOR_BGR2RGB);  // PP-OCRv6 rec 期望 RGB
  float ratio = static_cast<float>(crop.cols) / crop.rows;
  int w = static_cast<int>(std::round(recHeight * ratio));
  if (w > 1280) w = 1280;
  if (w < 1) w = 1;
  inW = w;
  cv::Mat resized;
  cv::resize(crop, resized, cv::Size(w, recHeight));
  resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);
  chw.assign(static_cast<size_t>(3) * recHeight * w, 0.0f);
  size_t plane = static_cast<size_t>(recHeight) * w;
  for (int y = 0; y < recHeight; ++y) {
    const cv::Vec3f* p = resized.ptr<cv::Vec3f>(y);
    for (int x = 0; x < w; ++x) {
      chw[0 * plane + y * w + x] = p[x][0] * 2.0f - 1.0f;
      chw[1 * plane + y * w + x] = p[x][1] * 2.0f - 1.0f;
      chw[2 * plane + y * w + x] = p[x][2] * 2.0f - 1.0f;
    }
  }
  return true;
}

std::string TextRecognizer::recPostprocess(const std::vector<float>& output, double& score) {
  int C = recOutC > 0 ? recOutC : static_cast<int>(dict.size()) + 1;
  if (C <= 1 || output.empty()) { score = 0.0; return ""; }
  int T = static_cast<int>(output.size()) / C;
  if (T <= 0) { score = 0.0; return ""; }
  std::string text;
  double confSum = 0.0;
  int confCount = 0;
  int prev = -1;
  for (int t = 0; t < T; ++t) {
    const float* row = output.data() + static_cast<size_t>(t) * C;
    int best = 0;
    float bestv = row[0];
    for (int c = 1; c < C; ++c) {
      if (row[c] > bestv) { bestv = row[c]; best = c; }
    }
    if (best != 0 && best != prev) {
      int charIdx = best - 1;
      if (charIdx < static_cast<int>(dict.size())) text += dict[charIdx];
      confSum += bestv;
      ++confCount;
    }
    prev = best;
  }
  score = confCount > 0 ? confSum / confCount : 0.0;
  if (score < 0.0) score = 0.0;
  if (score > 1.0) score = 1.0;
  return text;
}

bool TextRecognizer::resolveIndex(int32_t count, int32_t index, int32_t& out) {
  if (count <= 0) return false;
  if (index >= 0 && index < count) { out = index; return true; }
  if (index < 0 && -index <= count) { out = count + index; return true; }
  return false;
}

int32_t TextRecognizer::recognize(IImageBuffer* scene) {
  results.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!scene) { lastError = "recognize: null scene"; return 0; }
  if (!ensureLoaded()) { return 0; }
  auto start = std::chrono::steady_clock::now();
  cv::Mat sceneBgr = bufferToBgr(scene);
  if (sceneBgr.empty()) { lastError = "recognize: scene 格式不支持"; return 0; }
  // ROI
  cv::Mat searchArea = sceneBgr;
  cv::Point roiOffset(0, 0);
  if (useRoi) {
    cv::Rect full(0, 0, sceneBgr.cols, sceneBgr.rows);
    cv::Rect r = roi & full;
    if (r.width <= 0 || r.height <= 0) {
      lastError = "recognize: roi 越界";
      matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
      return 0;
    }
    searchArea = sceneBgr(r);
    roiOffset = r.tl();
  }
  // det
  std::vector<float> detInput;
  int inH = 0, inW = 0;
  detPreprocess(searchArea, detInput, inH, inW);
  std::vector<int64_t> detShape = {1, 3, inH, inW};
  std::vector<std::vector<float>> detOut;
  if (!detSession->runShaped({{detInName, detInput.data(), detShape}}, {detOutName}, detOut)) {
    lastError = "recognize: det 推理失败";
    matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return 0;
  }
  auto detOutShape = detSession->getOutputShape(detOutName);
  int ph = (detOutShape.size() >= 4 && detOutShape[2] > 0) ? static_cast<int>(detOutShape[2]) : inH;
  int pw = (detOutShape.size() >= 4 && detOutShape[3] > 0) ? static_cast<int>(detOutShape[3]) : inW;
  std::vector<cv::Rect> boxes = detPostprocess(detOut[0], ph, pw, searchArea.rows, searchArea.cols);
  // rec
  results.reserve(boxes.size());
  for (const auto& box : boxes) {
    std::vector<float> recInput;
    int recInW = 0;
    if (!recPreprocess(searchArea, box, recInput, recInW)) continue;
    std::vector<int64_t> recShape = {1, 3, recHeight, recInW};
    std::vector<std::vector<float>> recOut;
    if (!recSession->runShaped({{recInName, recInput.data(), recShape}}, {recOutName}, recOut)) continue;
    if (recOut.empty()) continue;
    double score = 0.0;
    std::string text = recPostprocess(recOut[0], score);
    if (text.empty()) continue;
    Result r;
    r.x = box.x + roiOffset.x;
    r.y = box.y + roiOffset.y;
    r.w = box.width;
    r.h = box.height;
    r.score = score;
    r.text = std::move(text);
    results.push_back(std::move(r));
  }
  matchTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
  return static_cast<int32_t>(results.size());
}

int32_t TextRecognizer::getMatchCount() {
  return static_cast<int32_t>(results.size());
}

const char* TextRecognizer::getMatch(int32_t index, OcrResult* out) {
  int32_t idx;
  if (!resolveIndex(static_cast<int32_t>(results.size()), index, idx)) return nullptr;
  const Result& r = results[idx];
  if (out) {
    out->x = r.x;
    out->y = r.y;
    out->w = r.w;
    out->h = r.h;
    out->score = r.score;
  }
  return r.text.c_str();
}

}
