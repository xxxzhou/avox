#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace avox {

// ITemplateMatcher 的 OpenCV 实现: cv::matchTemplate + NMS + green_mask
// 由 OpencvModule 注册为工厂 "opencv", 业务经 templateMatcherHub.create("opencv") 取实例
// 算法参考: MaaFramework/source/MaaFramework/Vision/TemplateMatcher
class TemplateMatcher : public ITemplateMatcher {
 private:
  // 单次命中 (内部用, 阈值字段记录来源模板, 跨 DLL 边界不暴露)
  struct Result {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    int32_t templateIndex = -1;  // 来源模板下标
    double score = 0.0;
    double threshold = 0.0;  // 生成该候选的模板阈值 (统一为越大越好坐标)
  };
  // 模板 (已转 BGR)
  struct Template {
    cv::Mat mat;
    double threshold = 0.7;
  };
  // 绿色(0,255,0)区域当透明: 生成 CV_8UC1 mask(绿=0忽略, 其余255); 无意义时返回空
  cv::Mat createMask(const cv::Mat& tmplBgr) const;
  // 全图扫描 matched 矩阵收集候选, score >= (threshold-0.2); roiOffset 为 ROI 左上偏移
  static void collectCandidates(const cv::Mat& matched, cv::Size tmplSize,
                                cv::Point roiOffset, int32_t templateIndex,
                                double threshold, std::vector<Result>& out);
  // 非极大值抑制: 按 score 降序, 交集/小框面积 >= nmsIoU 则抑制
  void nms(std::vector<Result>& results) const;
  // 按 orderBy 排序
  void sortResults(std::vector<Result>& results) const;
  // Python 风格负索引解析, 越界返回 false
  static bool resolveIndex(int32_t count, int32_t index, int32_t& out);

 private:
  TemplateMatchMethod method = TemplateMatchMethod::ccoeffNormed;
  MatchOrderBy orderBy = MatchOrderBy::horizontal;
  bool greenMask = false;
  bool grayscale = false;
  float nmsIoU = 0.2f;
  bool useRoi = false;
  cv::Rect roi;
  std::vector<Template> templates;
  std::vector<Result> results;
  float matchTimeMs = 0.0f;
  std::string lastError;

 public:
  TemplateMatcher();
  ~TemplateMatcher() override;

 public:
  void setMethod(TemplateMatchMethod method_) override;
  void setOrderBy(MatchOrderBy orderBy_) override;
  void setGreenMask(bool enable) override;
  void setGrayscale(bool enable) override;
  void setNmsIoU(float iou) override;
  void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void clearRoi() override;
  int32_t addTemplate(IImageBuffer* tmpl, double threshold) override;
  int32_t addTemplatePath(const char* path, double threshold) override;
  void clearTemplates() override;
  int32_t match(IImageBuffer* scene) override;
  // match 实现（match = try/catch 装甲壳，cv 异常不 terminate 宿主进程）
  int32_t matchImpl(IImageBuffer* scene);
  int32_t getMatchCount() override;
  bool getMatch(int32_t index, MatchResult* out) override;
  float getMatchTimeMs() override;
  const char* getLastError() override;
};

}
