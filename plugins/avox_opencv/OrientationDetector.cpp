#include "OrientationDetector.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

namespace avox {

OrientationDetector::OrientationDetector() = default;
OrientationDetector::~OrientationDetector() = default;

// ============ 配置 ============
void OrientationDetector::setCircle(int32_t cx_, int32_t cy_, int32_t radius_) {
  cx = cx_;
  cy = cy_;
  radius = radius_;
  hasCircle = (radius > 0);
}

// BGI CameraOrientationFromGia 是固定算法, setAngleRange/setSmooth 不适用(仅存储, 不影响结果)。
void OrientationDetector::setAngleRange(int32_t angleMin_, int32_t angleMax_) {
  angleMin = angleMin_;
  angleMax = angleMax_;
}
void OrientationDetector::setSmooth(int32_t degrees) { smooth = degrees; }

namespace {
// BGI FindPeaks: data[i] > data[i-1] && data[i] > data[i+1], 返回索引
std::vector<int> findPeaks(const std::vector<float>& data) {
  std::vector<int> peaks;
  peaks.reserve(data.size() / 8);
  for (size_t i = 1; i + 1 < data.size(); ++i) {
    if (data[i] > data[i - 1] && data[i] > data[i + 1]) {
      peaks.push_back(static_cast<int>(i));
    }
  }
  return peaks;
}

// BGI Shift: k>0 右移(元素向高下标), k<0 左移; 360 循环
std::vector<int> shiftArr(const std::vector<int>& arr, int k) {
  const int n = static_cast<int>(arr.size());
  std::vector<int> out(n);
  if (k >= 0) {  // RightShift: 元素 i → (i+k)%n
    for (int i = 0; i < n; ++i) out[(i + k) % n] = arr[i];
  } else {  // LeftShift: 元素 i → (i-|k|+n)%n
    const int m = -k;
    for (int i = 0; i < n; ++i) out[(i - m + n) % n] = arr[i];
  }
  return out;
}
}  // namespace

// ============ 执行 ============
// 关键几何: cv::warpPolar 输出 行=角度(0-359), 列=半径(0-359)（实测确认）。
// 返回角度为 BGI 原始约定（DrawDirection 画线看: 0=箭头指向右/东, 顺时针;
// 实际取值 [45,360]）。精确约定待实机标定, 调用方(Python)按需转换。
double OrientationDetector::compute(IImageBuffer* scene) {
  AVOX_CV_TRY;
  lastAngle = -1.0;
  lastScore = 0.0;
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!scene) {
    lastError = "compute: null scene";
    return -1.0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat bgr = imageBufferToBgr(scene);
  if (bgr.empty()) {
    lastError = "compute: unsupported scene format";
    return -1.0;
  }
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

  // 1. 高斯模糊
  cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);
  // 2. 极坐标展开: 中心=setCircle(缺省图像中心), maxRadius=360, 输出 360×360
  const cv::Point2f center =
      (hasCircle && cx > 0 && cy > 0)
          ? cv::Point2f(static_cast<float>(cx), static_cast<float>(cy))
          : cv::Point2f(gray.cols / 2.0f, gray.rows / 2.0f);
  cv::Mat polar;
  cv::warpPolar(gray, polar, cv::Size(360, 360), center, 360.0,
                cv::INTER_LINEAR | cv::WARP_POLAR_LINEAR);
  // 3. ROI: 半径带 10-80 (列=半径), 全角度(行) → 逆时针 90°(角度变列)
  cv::Mat roi = polar(cv::Rect(10, 0, 70, polar.rows));
  cv::Mat rot;
  cv::rotate(roi, rot, cv::ROTATE_90_COUNTERCLOCKWISE);
  // 4. Scharr dx (沿列=角度)
  cv::Mat scharr;
  cv::Scharr(rot, scharr, CV_32F, 1, 0);
  // 5. 波峰: 展平后按列(角度)计数正峰→left, 负峰→right
  const float* p = scharr.ptr<float>(0);
  std::vector<float> data(p, p + scharr.total());
  std::vector<int> left(360, 0), right(360, 0);
  for (int idx : findPeaks(data)) left[idx % 360]++;
  std::vector<float> rev(data.size());
  for (size_t i = 0; i < data.size(); ++i) rev[i] = -data[i];
  for (int idx : findPeaks(rev)) right[idx % 360]++;
  // 6. 优化: left2 = max(left-right,0), right2 = max(right-left,0)
  std::vector<int> left2(360), right2(360);
  for (int a = 0; a < 360; ++a) {
    left2[a] = std::max(left[a] - right[a], 0);
    right2[a] = std::max(right[a] - left[a], 0);
  }
  // 7. 卷积: left2 × Shift(right2, -90+i) 加权 (BGI 整数除法忠实: x*y*(3-|i|)/3)
  std::vector<int> sum(360, 0);
  for (int i = -2; i <= 2; ++i) {
    const std::vector<int> shifted = shiftArr(right2, -90 + i);
    const int w = 3 - std::abs(i);
    for (int a = 0; a < 360; ++a) sum[a] += left2[a] * shifted[a] * w / 3;
  }
  // 8. 二次卷积: Shift(sum, i) 加权
  std::vector<int> result(360, 0);
  for (int i = -2; i <= 2; ++i) {
    const std::vector<int> shifted = shiftArr(sum, i);
    const int w = 3 - std::abs(i);
    for (int a = 0; a < 360; ++a) result[a] += shifted[a] * w / 3;
  }
  // 9. 角度 = maxIndex + 45 (BGI: 仅 >360 时减 360, 可取 360)
  const auto it = std::max_element(result.begin(), result.end());
  const int maxIndex = static_cast<int>(it - result.begin());
  int angle = maxIndex + 45;
  if (angle > 360) angle -= 360;
  // score: 峰值/总和 的粗略置信度 [0,1]
  const double total = std::accumulate(result.begin(), result.end(), 0.0);
  lastScore = total > 0.0 ? std::min(1.0, static_cast<double>(*it) / total) : 0.0;
  lastAngle = static_cast<double>(angle);
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  return static_cast<double>(angle);
  AVOX_CV_CATCH_RET(0.0);
}

double OrientationDetector::getLastScore() { return lastScore; }

float OrientationDetector::getMatchTimeMs() { return matchTimeMs; }

const char* OrientationDetector::getLastError() { return lastError.c_str(); }

}
