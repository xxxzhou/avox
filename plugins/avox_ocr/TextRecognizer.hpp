#pragma once

#include "avox/AvoxVision.h"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModelUser.hpp"

#include <opencv2/opencv.hpp>

#include <memory>
#include <string>
#include <vector>

namespace avox {

// ITextRecognizer 的 PP-OCRv6 实现 (det DBNet + rec CRNN/CTC)
// 由 OcrModule 注册为工厂 "ppocr", 业务经 createTextRecognizer() 取实例
// 推理走 avox_onnx 的 IONNXSession (onnxSessionHub.create("onnx")); det 用 runShaped(动态 H/W)
// 后处理参考: RapidAI/RapidOcrOnnx
class TextRecognizer : public ITextRecognizer, public OnnxModelUser {
 private:
  // 识别结果 (内部用, 含文字串; 边界不暴露 std::string)
  struct Result {
    int32_t x = 0, y = 0, w = 0, h = 0;
    double score = 0.0;
    std::string text;
  };
  // IImageBuffer -> BGR(CV_8UC3), 自包含(不依赖 avox_opencv 的 OpencvHelper)
  cv::Mat bufferToBgr(IImageBuffer* buf);
  // det 预处理: 限长边 960 + 对齐 32 + ImageNet 归一化 -> CHW float; 返回输入 H/W
  bool detPreprocess(const cv::Mat& bgr, std::vector<float>& chw, int& inH, int& inW);
  // det 后处理: 概率图 -> 二值化 -> findContours -> 框
  std::vector<cv::Rect> detPostprocess(const std::vector<float>& probMap, int ph, int pw,
                                        int srcH, int srcW);
  // rec 预处理: 按 box crop -> BGR→RGB -> resize -> 归一化 -> CHW float
  bool recPreprocess(const cv::Mat& bgr, const cv::Rect& box, std::vector<float>& chw, int& inW);
  // rec 后处理: CTC 解码 + 字典查表 -> 文字串 + 平均置信度
  std::string recPostprocess(const std::vector<float>& output, double& score);
  // Python 风格负索引解析, 越界返回 false
  static bool resolveIndex(int32_t count, int32_t index, int32_t& out);
  // 懒加载: 首次取 det/rec session + 读 input/output 名 + 字典; 幂等 (detSession 非空直接 true)
  bool ensureLoaded();

 private:
  IONNXSession* detSession = nullptr;  // 借用自 OnnxSessionCache(Shared), 对象不释放
  IONNXSession* recSession = nullptr;  // 借用自 OnnxSessionCache(Shared), 对象不释放
  std::string detInName, detOutName;
  std::string recInName, recOutName;
  int recHeight = 48;   // rec 输入高 (从模型 input shape 读)
  int recOutC = 0;      // rec 输出类别数(含 blank)
  std::vector<std::string> dict;  // 字符字典

 private:
  double threshold = 0.3;  // det 二值化阈值
  bool useGpu = false;
  bool useRoi = false;
  cv::Rect roi;
  std::vector<Result> results;  // 最近一次 recognize 的结果缓存
  float matchTimeMs = 0.0f;
  std::string lastError;

 public:
  TextRecognizer();
  ~TextRecognizer() override;

 public:
  void setThreshold(double threshold_) override { threshold = threshold_; }
  void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void clearRoi() override { useRoi = false; }
  void setUseGpu(bool enable) override { useGpu = enable; }
  int32_t recognize(IImageBuffer* scene) override;
  int32_t getMatchCount() override;
  const char* getMatch(int32_t index, OcrResult* out) override;
  float getMatchTimeMs() override { return matchTimeMs; }
  const char* getLastError() override { return lastError.c_str(); }
};

}
