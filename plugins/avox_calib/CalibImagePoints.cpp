#include "CalibImagePoints.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

// CalibArucoType → cv::aruco::PredefinedDictionaryType (共享实现见 CalibHelper.cpp)

void CalibImagePoints::reset() { points.clear(); }

void CalibImagePoints::setChessboardInfo(const ChessboardInfo& info) {
  cornerType = CalibCornerType::chessboard;
  chessboardInfo = info;
  reset();
  // 行优先生成平面角点: 左上为原点, x 向右 y 向下 (与 OpenCV 图像坐标一致)
  for (int32_t y = 0; y < info.height; ++y) {
    for (int32_t x = 0; x < info.width; ++x) {
      points.emplace_back(info.size * x, info.size * y, 0.0f);
    }
  }
}

void CalibImagePoints::setArucoInfo(CalibArucoType arucoType, int32_t startArucoId_) {
  cornerType = CalibCornerType::aruco;
  startArucoId = startArucoId_;
  auto dict = cv::aruco::getPredefinedDictionary(getCvArucoType(arucoType));
  dictionary = cv::makePtr<cv::aruco::Dictionary>(dict);
  // 亚像素精确定位; winSize 与焦段相关, 焦段越近值越小越好
  auto parameters = cv::makePtr<cv::aruco::DetectorParameters>();
  parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
  parameters->cornerRefinementWinSize = 3;
  parameters->cornerRefinementMaxIterations = 50;
  parameters->cornerRefinementMinAccuracy = 0.01;
  detector = cv::makePtr<cv::aruco::ArucoDetector>(*dictionary, *parameters);
  reset();
}

void CalibImagePoints::setPoints(int32_t count, const vec3f* pts) {
  // 只替换 3D 点, 不改识别类型 (可在 setChessboardInfo 后覆盖为非平面点集,
  // 消除平面目标 PnP 二义性对解算稳定性的影响)
  points.clear();
  points.reserve(count);
  for (int32_t i = 0; i < count; i++) {
    points.emplace_back(pts[i].x, pts[i].y, pts[i].z);
  }
}

int32_t CalibImagePoints::getPointCount() { return (int32_t)points.size(); }

vec3f CalibImagePoints::getPoint(int32_t index) {
  if (index >= 0 && index < (int32_t)points.size()) {
    return vec3f(points[index].x, points[index].y, points[index].z);
  }
  return vec3f(0.0f, 0.0f, 0.0f);
}

CornerMass CalibImagePoints::getCornerMass() { return cornerMass; }

CalibCornerType CalibImagePoints::getCornerType() { return cornerType; }

bool CalibImagePoints::computeChessboard(cv::Mat& cameraImage,
                                         std::vector<cv::Point2f>& corners,
                                         std::vector<cv::Point3f>& cpoints) {
  cv::Mat grayMat;
  cv::cvtColor(cameraImage, grayMat, cv::COLOR_RGBA2GRAY);
  cv::Size chessSize(chessboardInfo.width, chessboardInfo.height);
  const bool bCornersFound = cv::findChessboardCorners(
      grayMat, chessSize, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
  if (!bCornersFound) {
    LOGFLF(LogLevel::warn, "image not find chessboard");
    return false;
  }
  // 亚像素精确角点
  cv::TermCriteria criteria(cv::TermCriteria::EPS | cv::TermCriteria::MAX_ITER, 30, 0.001);
  cv::cornerSubPix(grayMat, corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);
  cpoints.resize(points.size());
  memcpy(cpoints.data(), points.data(), sizeof(cv::Point3f) * points.size());
  return true;
}

bool CalibImagePoints::computeAruco(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                                    std::vector<cv::Point3f>& arucoPoints) {
  cv::Mat grayMat;
  cv::cvtColor(cameraImage, grayMat, cv::COLOR_RGBA2GRAY);
  markerIds.clear();
  markerCorners.clear();
  rejected.clear();
  detector->detectMarkers(grayMat, markerCorners, markerIds, rejected);
  cornerMass = CornerMass();
  cornerMass.visible = (int32_t)markerIds.size();
  cornerMass.rejected = (int32_t)rejected.size();
  if (markerIds.empty()) {
    return false;
  }
  // 每个 Aruco 4 角点, 按 arucoId 契约配对 3D 点
  const int32_t arucoGroup = 4;
  int32_t markerCount = (int32_t)markerIds.size();
  corners.resize(markerCount * arucoGroup);
  arucoPoints.resize(markerCount * arucoGroup);
  cv::Point2f imageSize = {(float)cameraImage.cols, (float)cameraImage.rows};
  double similarity = 0;
  float area = 0;
  // Aruco 码含白边, 面积按 (bits+2)/bits 比例还原
  float borderScale = float(dictionary->markerSize + 2) / dictionary->markerSize;
  float borderScale2 = borderScale * borderScale;
  for (int32_t i = 0; i < markerCount; i++) {
    int32_t pointId = (markerIds[i] - startArucoId) * arucoGroup;
    if (pointId < 0 || pointId >= (int32_t)points.size()) {
      LOGFLF(LogLevel::warn, "imagepoint count:", (int32_t)points.size(),
             " but find pointId:", pointId, ", make sure imagepoints match aruco image");
      return false;
    }
    for (int32_t c = 0; c < arucoGroup; c++) {
      arucoPoints[i * arucoGroup + c] = points[pointId + c];
      corners[i * arucoGroup + c] = markerCorners[i][c];
    }
    // 模糊度: 仿射校正到正方形后与标准 Aruco 图比 SSIM
    cv::RotatedRect rotateRect = cv::minAreaRect(markerCorners[i]);
    cv::Point2f srcPts[3] = {markerCorners[i][0], markerCorners[i][1], markerCorners[i][2]};
    cv::Point2f dstPts[3] = {cv::Point2f(0, 0), cv::Point2f(rotateRect.size.width, 0),
                             cv::Point2f(rotateRect.size.width, rotateRect.size.height)};
    cv::Mat affineMat = cv::getAffineTransform(srcPts, dstPts);
    cv::Mat subImage;
    cv::warpAffine(grayMat, subImage, affineMat, rotateRect.size);
    cv::Size2i markerSize = rotateRect.size;
    int32_t markerLength = std::min(markerSize.width, markerSize.height);
    markerSize.width = markerLength;
    markerSize.height = markerLength;
    cv::resize(subImage, subImage, markerSize);
    double minValue = 0, maxValue = 0;
    cv::minMaxLoc(subImage, &minValue, &maxValue);
    cv::Mat arucoMat;
    dictionary->generateImageMarker(markerIds[i], markerLength, arucoMat);
    // 亮度归一化, 降低屏幕亮度/曝光造成的影响
    cv::normalize(arucoMat, arucoMat, minValue, maxValue, cv::NORM_MINMAX, CV_8U);
    similarity += calibCompareSSIM(arucoMat, subImage);
    area += (float)rotateRect.size.area() * borderScale2;
  }
  cornerMass.visibility = area / (imageSize.x * imageSize.y);
  cornerMass.blur = (float)(similarity / markerCount);
  return true;
}

bool CalibImagePoints::findCorners(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                                   std::vector<cv::Point3f>& cpoints) {
  bool bFind = false;
  if (cornerType == CalibCornerType::chessboard) {
    bFind = computeChessboard(cameraImage, corners, cpoints);
  } else if (cornerType == CalibCornerType::aruco) {
    bFind = computeAruco(cameraImage, corners, cpoints);
  } else {
    LOGFLF(LogLevel::warn, "image points not support current cornerType: ", (int32_t)cornerType);
  }
  return bFind;
}

bool CalibImagePoints::computeCameraPose(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                                         std::vector<cv::Point3f>& cpoints,
                                         const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
                                         cv::Mat& rot, cv::Mat& tra, bool bContinued) {
  bool bFind = findCorners(cameraImage, corners, cpoints);
  if (!bFind) {
    return false;
  }
  // PnP: 标定板在摄像机坐标系下姿态
  if (!cv::solvePnP(cpoints, corners, cameraMatrix, distCoeffs, rot, tra, bContinued)) {
    LOGFLF(LogLevel::warn, "compute image get camera pose error");
    return false;
  }
  return true;
}

}  // namespace avox
