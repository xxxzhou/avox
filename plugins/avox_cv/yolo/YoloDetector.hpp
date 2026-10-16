#pragma once

#include "avox/AvoxVision.h"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModelUser.hpp"

#include <opencv2/opencv.hpp>

#include <memory>
#include <string>
#include <vector>

namespace avox {

// IYoloDetector 的通用 Ultralytics YOLO 实现 (检测 detect / 分类 classify)。
// 由 CvModule 注册为工厂 "yolo", 业务经 createYoloDetector() 取实例。
// 推理走 avox_onnx 的 IONNXSession (onnxSessionHub.create("onnx")); 输入 H/W 用 runShaped。
class YoloDetector : public IYoloDetector, public OnnxModelUser {
 private:
  // IImageBuffer -> BGR(CV_8UC3), 自包含(不依赖 avox_opencv 的 OpencvHelper)
  cv::Mat bufferToBgr(IImageBuffer* buf);
  // 解析 Ultralytics names 元数据 "{0: 'a', 1: 'b_c'}" -> 下标即类 id 的串表, '_' -> ' '
  void parseNames(const std::string& raw);
  // letterbox (等比 min 缩放 + 居中黑底零填充) -> RGB NCHW float; 返回 scale/padW/padH
  void letterbox(const cv::Mat& bgr, std::vector<float>& chw,
                 float& scale, int& padW, int& padH);
  // 精确 resize (拉伸到 imgsz×imgsz, 无 padding) -> RGB NCHW float (classify 用)
  void exactResize(const cv::Mat& bgr, std::vector<float>& chw);
  // 检测后处理: [1,4+nc,N] -> 反 letterbox -> floor -> 按类贪心 NMS; 写入 detections_
  void decodeDetect(const std::vector<float>& out, int channels, int numAnchors,
                    int origW, int origH, float scale, int padW, int padH,
                    float confThresh, float iouThresh);
  // 懒加载: 首次取 session + 读 in/out 名 + 元数据(task/names/imgsz); 幂等 (session 非空直接 true)
  bool ensureLoaded();

 private:
  // 配置
  std::string modelPath;
  bool useGpu = false;
  float confThreshold = 0.3f;
  float iouThreshold = 0.45f;
  // 加载态
  IONNXSession* session = nullptr;  // 借用自 OnnxSessionCache(Shared), 对象不释放
  std::string inName, outName;
  std::string task;             // "detect"/"classify" (默认 detect)
  std::vector<std::string> names;  // 下标即类 id
  int imgsz = 640;
  int numClasses = 0;
  // 结果缓存
  std::vector<YoloBox> detections;
  float classifyScore = 0.0f;
  float matchTimeMs = 0.0f;
  std::string lastError;

 public:
  YoloDetector() = default;
  ~YoloDetector() override = default;

 public:
  void setModelPath(const char* path) override;
  void setUseGpu(bool enable) override { useGpu = enable; }
  void setConfThreshold(float conf) override { confThreshold = conf; }
  void setIouThreshold(float iou) override { iouThreshold = iou; }
  const char* getTask() override;
  int32_t getClassCount() override;
  const char* getClassName(int32_t classId) override;
  int32_t detect(IImageBuffer* scene) override;
  int32_t getDetectionCount() override;
  bool getDetection(int32_t index, YoloBox* out) override;
  int32_t classify(IImageBuffer* scene) override;
  float getClassifyScore() override { return classifyScore; }
  float getMatchTimeMs() override { return matchTimeMs; }
  const char* getLastError() override { return lastError.c_str(); }
};

}
