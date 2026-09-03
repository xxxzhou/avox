#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace avox {

// IFeatureMatcher 的 OpenCV 实现: SIFT/ORB/AKAZE + BFMatcher + Lowe 比率 + RANSAC 单应性。
// 由 OpencvModule 注册为工厂 "opencv", 业务经 featureMatcherHub.create("opencv") 取实例。
// 参考点: 在 scene 中定位"参考图"(旋转/尺度不变), 命中框=参考图四角经 H 投影的轴对齐外接矩形。
class FeatureMatcher : public IFeatureMatcher {
 private:
  // 已提取特征的参考图 (内部用, 跨 DLL 边界不暴露)
  struct Ref {
    cv::Mat mat;                         // BGR
    std::vector<cv::KeyPoint> kps;
    cv::Mat desc;
  };

  // BGI 大地图: 预存底图(train)的切块缓存 (跨 DLL 边界不暴露)
  struct TrainBlock {
    std::vector<cv::KeyPoint> kps;
    cv::Mat desc;
    std::vector<int> idx;               // 仅 split 临时用: 全局 kp 下标 (建 desc 后即弃)
  };
  struct TrainCache {
    bool valid = false;
    int32_t imgW = 0, imgH = 0;          // 底图像素
    int32_t rows = 0, cols = 0;          // 切块网格
    int32_t cellW = 0, cellH = 0;        // imgW/cols, imgH/rows (整数除法)
    std::vector<cv::KeyPoint> allKps;    // 全图特征 (全图 Match 兜底用)
    cv::Mat allDesc;                      // 全图描述子 (128×N, CV_32FC1)
    std::vector<TrainBlock> grid;         // rows×cols, 索引 r*cols+c; 坐标不偏移
  };

  FeatureMethod method = FeatureMethod::sift;
  int32_t maxFeatures = 1000;
  float ratioThreshold = 0.75f;
  int32_t minInliers = 8;
  bool useRoi = false;
  cv::Rect roi;
  std::vector<Ref> refs;
  TrainCache train;                      // BGI 预存底图缓存 (loadTrainFeaturesPath 填充)
  std::vector<FeatureMatchResult> results;
  float matchTimeMs = 0.0f;
  std::string lastError;

  // 按 method 构造检测器 (SIFT/ORB 用 nfeatures; AKAZE 无该参数)
  cv::Ptr<cv::Feature2D> makeDetector() const;
  // 提取参考图特征 (detectAndCompute); 失败返回 false
  bool extractRef(IImageBuffer* buf, Ref& out);
  // Python 风格负索引解析, 越界返回 false
  static bool resolveIndex(int32_t count, int32_t index, int32_t& out);
  // ---- BGI 大地图: 切块/合并/定位辅助 (实现见 .cpp) ----
  bool loadTrainFeaturesPathImpl(const char* kpPath, const char* descPath,
                                 int32_t imgW, int32_t imgH,
                                 int32_t blockRows, int32_t blockCols);
  void splitTrainFeatures();             // allKps/allDesc → grid (坐标不偏移)
  bool mergeTrainFeaturesInRange(const cv::Rect& roi, int32_t expandCells,
                                 std::vector<cv::KeyPoint>& outKps,
                                 cv::Mat& outDesc) const;  // 取 roi 覆盖格 ±expandCells 合并
  bool knnLocate(const std::vector<cv::KeyPoint>& qKps, const cv::Mat& qDesc,
                 int32_t queryW, int32_t queryH,
                 const std::vector<cv::KeyPoint>& tKps, const cv::Mat& tDesc,
                 cv::Point2d& outTrainCenter, double& outScore);  // BGI KnnMatch
  bool matchLocate(const std::vector<cv::KeyPoint>& qKps, const cv::Mat& qDesc,
                   int32_t queryW, int32_t queryH,
                   const std::vector<cv::KeyPoint>& tKps, const cv::Mat& tDesc,
                   cv::Point2d& outTrainCenter, double& outScore);  // BGI Match 兜底

 public:
  FeatureMatcher();
  ~FeatureMatcher() override;

 public:
  void setMethod(FeatureMethod method_) override;
  void setMaxFeatures(int32_t n) override;
  void setRatioThreshold(float ratio) override;
  void setMinInliers(int32_t n) override;
  void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void clearRoi() override;
  int32_t addReference(IImageBuffer* ref) override;
  int32_t addReferencePath(const char* path) override;
  void clearReferences() override;
  int32_t match(IImageBuffer* scene) override;
  bool loadTrainFeaturesPath(const char* kpPath, const char* descPath,
                             int32_t imgW, int32_t imgH,
                             int32_t blockRows, int32_t blockCols) override;
  int32_t matchQueryLocal(IImageBuffer* query,
                          int32_t roiX, int32_t roiY, int32_t roiW, int32_t roiH,
                          int32_t expandCells) override;
  int32_t matchQueryFull(IImageBuffer* query) override;
  int32_t getMatchCount() override;
  bool getMatch(int32_t index, FeatureMatchResult* out) override;
  float getMatchTimeMs() override;
  const char* getLastError() override;
};

}
