#include "CalibHelper.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace avox {

cv::aruco::PredefinedDictionaryType getCvArucoType(CalibArucoType arucoType) {
  switch (arucoType) {
    case CalibArucoType::dict4x4_50: return cv::aruco::DICT_4X4_50;
    case CalibArucoType::dict4x4_100: return cv::aruco::DICT_4X4_100;
    case CalibArucoType::dict4x4_250: return cv::aruco::DICT_4X4_250;
    case CalibArucoType::dict4x4_1000: return cv::aruco::DICT_4X4_1000;
    case CalibArucoType::dict5x5_50: return cv::aruco::DICT_5X5_50;
    case CalibArucoType::dict5x5_100: return cv::aruco::DICT_5X5_100;
    case CalibArucoType::dict5x5_250: return cv::aruco::DICT_5X5_250;
    case CalibArucoType::dict5x5_1000: return cv::aruco::DICT_5X5_1000;
    case CalibArucoType::dict6x6_50: return cv::aruco::DICT_6X6_50;
    case CalibArucoType::dict6x6_100: return cv::aruco::DICT_6X6_100;
    case CalibArucoType::dict6x6_250: return cv::aruco::DICT_6X6_250;
    case CalibArucoType::dict6x6_1000: return cv::aruco::DICT_6X6_1000;
    case CalibArucoType::dict7x7_50: return cv::aruco::DICT_7X7_50;
    case CalibArucoType::dict7x7_100: return cv::aruco::DICT_7X7_100;
    case CalibArucoType::dict7x7_250: return cv::aruco::DICT_7X7_250;
    case CalibArucoType::dict7x7_1000: return cv::aruco::DICT_7X7_1000;
    case CalibArucoType::dictArucoOriginal: return cv::aruco::DICT_ARUCO_ORIGINAL;
    case CalibArucoType::dictApriltag16h5: return cv::aruco::DICT_APRILTAG_16h5;
    case CalibArucoType::dictApriltag25h9: return cv::aruco::DICT_APRILTAG_25h9;
    case CalibArucoType::dictApriltag36h10: return cv::aruco::DICT_APRILTAG_36h10;
    case CalibArucoType::dictApriltag36h11: return cv::aruco::DICT_APRILTAG_36h11;
    default: return cv::aruco::DICT_6X6_1000;
  }
}

int32_t calibGetMatType(const ImageType& imageType) {
  switch (imageType) {
    case ImageType::rgba8:
    case ImageType::bgra8:
      return CV_8UC4;
    case ImageType::rgb8:
    case ImageType::bgr8:
      return CV_8UC3;
    case ImageType::r16:
      return CV_16UC1;
    case ImageType::r32f:
      return CV_32FC1;
    case ImageType::r8:
      return CV_8UC1;
    default:
      return CV_8UC4;
  }
}

cv::Mat calibImage2Mat(IImageBuffer* buffer) {
  ImageFormat fmt = buffer->getImageFormat();
  int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * getPixelSize(fmt.imageType);
  cv::Mat mat(fmt.height, fmt.width, calibGetMatType(fmt.imageType));
  const uint8_t* src = buffer->getPointer();
  // 逐行拷 (rowPitch 与 cv step 可能不同), bgra 再转 rgba 统一处理
  if (mat.step == (size_t)pitch) {
    memcpy(mat.data, src, (size_t)pitch * fmt.height);
  } else {
    for (int32_t r = 0; r < fmt.height; r++) {
      memcpy(mat.data + r * mat.step, src + r * pitch, (size_t)pitch);
    }
  }
  if (fmt.imageType == ImageType::bgra8) {
    cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGBA);
  }
  return mat;
}

void calibMat4x42Mat(const Mat4x4d& transform, cv::Mat& rotator, cv::Mat& position) {
  // avox 行主序 m(r,c) → cv (i,j)=m(j,i): cv 矩阵是数学列主序语义, 即旋转块转置
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      rotator.at<double>(i, j) = (&transform.row0.x)[j * 4 + i];
    }
    position.at<double>(i) = (&transform.row3.x)[i];
  }
}

void calibMat2Mat4x4(const cv::Mat& rotator, const cv::Mat& position, Mat4x4d& transform) {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      (&transform.row0.x)[i * 4 + j] = rotator.at<double>(j, i);
    }
    (&transform.row0.x)[i * 4 + 3] = 0.0;
    (&transform.row3.x)[i] = position.at<double>(i);
  }
  (&transform.row3.x)[3] = 1.0;
}

void calibMat2Mat4x4(const cv::Mat& rotator, const cv::Mat& position, cv::Mat& transform) {
  // 输出可能为空, 防御性创建 (aoce 原版靠调用方定尺寸, 空矩阵 .at 直接崩)
  if (transform.rows != 4 || transform.cols != 4) {
    transform.create(4, 4, CV_64F);
  }
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      transform.at<double>(i, j) = rotator.at<double>(i, j);
    }
    transform.at<double>(i, 3) = position.at<double>(i);
  }
  transform.at<double>(3, 0) = 0.0;
  transform.at<double>(3, 1) = 0.0;
  transform.at<double>(3, 2) = 0.0;
  transform.at<double>(3, 3) = 1.0;
}

void calibMat2Mat4x4(const cv::Mat& cvTrans, Mat4x4d& transform) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      (&transform.row0.x)[i * 4 + j] = cvTrans.at<double>(j, i);
    }
  }
}

void calibMat4x42Mat(const Mat4x4d& transform, cv::Mat& cvTrans) {
  // 输出可能为空, 防御性创建
  if (cvTrans.rows != 4 || cvTrans.cols != 4) {
    cvTrans.create(4, 4, CV_64F);
  }
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      cvTrans.at<double>(i, j) = (&transform.row0.x)[j * 4 + i];
    }
  }
}

void calibLensModelToCV(const LensModel& lensModel, cv::Mat& cameraMatrix, cv::Mat& distCoeffs) {
  cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
  distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
  vec2d size = lensModel.imageSize;
  cameraMatrix.at<double>(0, 0) = lensModel.focalLength.x * size.x;
  cameraMatrix.at<double>(1, 1) = lensModel.focalLength.y * size.y;
  cameraMatrix.at<double>(0, 2) = lensModel.focalCenter.x * size.x;
  cameraMatrix.at<double>(1, 2) = lensModel.focalCenter.y * size.y;
  distCoeffs.at<double>(0) = lensModel.k1;
  distCoeffs.at<double>(1) = lensModel.k2;
  distCoeffs.at<double>(2) = lensModel.p1;
  distCoeffs.at<double>(3) = lensModel.p2;
  distCoeffs.at<double>(4) = lensModel.k3;
}

void calibLensCVToModel(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
                        const vec2d& imageSize, LensModel& lensModel) {
  lensModel.imageSize = imageSize;
  lensModel.focalCenter = vec2d(cameraMatrix.at<double>(0, 2) / imageSize.x,
                                cameraMatrix.at<double>(1, 2) / imageSize.y);
  lensModel.focalLength = vec2d(cameraMatrix.at<double>(0, 0) / imageSize.x,
                                cameraMatrix.at<double>(1, 1) / imageSize.y);
  lensModel.k1 = distCoeffs.at<double>(0);
  lensModel.k2 = distCoeffs.at<double>(1);
  lensModel.p1 = distCoeffs.at<double>(2);
  lensModel.p2 = distCoeffs.at<double>(3);
  lensModel.k3 = distCoeffs.at<double>(4);
}

cv::Mat calibHomogeneousInverse(const cv::Mat& T) {
  cv::Mat R = T(cv::Rect(0, 0, 3, 3));
  cv::Mat t = T(cv::Rect(3, 0, 1, 3));
  cv::Mat Rt = R.t();
  cv::Mat tinv = -Rt * t;
  cv::Mat Tinv = cv::Mat::eye(4, 4, T.type());
  Rt.copyTo(Tinv(cv::Rect(0, 0, 3, 3)));
  tinv.copyTo(Tinv(cv::Rect(3, 0, 1, 3)));
  return Tinv;
}

cv::Mat calibHomogeneousInverse(const cv::Mat& rotMat, const cv::Mat& trans) {
  cv::Mat Rt;
  if (rotMat.cols == 3 && rotMat.rows == 3) {
    Rt = rotMat.t();
  } else {
    // rotMat 为轴角 (Rodrigues 3x1), 先转 3x3
    cv::Mat rotMatx;
    cv::Rodrigues(rotMat, rotMatx);
    Rt = rotMatx.t();
  }
  cv::Mat tinv = -Rt * trans;
  cv::Mat Tinv = cv::Mat::eye(4, 4, CV_64F);
  Rt.copyTo(Tinv(cv::Rect(0, 0, 3, 3)));
  tinv.copyTo(Tinv(cv::Rect(3, 0, 1, 3)));
  return Tinv;
}

void calibNormalRot(cv::Mat& rotate) {
  cv::Mat u, w, v;
  cv::SVD::compute(rotate, w, u, v);
  rotate = u * v;
}

// ============ Tsai 手眼 + scale 列改进 ============
// Reference: R. Y. Tsai and R. K. Lenz, "A new technique for fully autonomous and
// efficient 3D robotics hand/eye calibration." IEEE Trans. Robotics and Automation, 1989.
// scale 列改进见 doc/plan/虚拟制片标定移植方案.md §1.3④

// 半四元数: q = sin(theta/2)*v, R → 3x1
static cv::Mat rot2quatMinimal(const cv::Mat& R) {
  double m00 = R.at<double>(0, 0), m01 = R.at<double>(0, 1), m02 = R.at<double>(0, 2);
  double m10 = R.at<double>(1, 0), m11 = R.at<double>(1, 1), m12 = R.at<double>(1, 2);
  double m20 = R.at<double>(2, 0), m21 = R.at<double>(2, 1), m22 = R.at<double>(2, 2);
  double trace = m00 + m11 + m22;
  double qx, qy, qz;
  if (trace > 0) {
    double S = sqrt(trace + 1.0) * 2;
    qx = (m21 - m12) / S;
    qy = (m02 - m20) / S;
    qz = (m10 - m01) / S;
  } else if (m00 > m11 && m00 > m22) {
    double S = sqrt(1.0 + m00 - m11 - m22) * 2;
    qx = 0.25 * S;
    qy = (m01 + m10) / S;
    qz = (m02 + m20) / S;
  } else if (m11 > m22) {
    double S = sqrt(1.0 + m11 - m00 - m22) * 2;
    qx = (m01 + m10) / S;
    qy = 0.25 * S;
    qz = (m12 + m21) / S;
  } else {
    double S = sqrt(1.0 + m22 - m00 - m11) * 2;
    qx = (m02 + m20) / S;
    qy = (m12 + m21) / S;
    qz = 0.25 * S;
  }
  return (cv::Mat_<double>(3, 1) << qx, qy, qz);
}

static cv::Mat skew(const cv::Mat& v) {
  double vx = v.at<double>(0, 0);
  double vy = v.at<double>(1, 0);
  double vz = v.at<double>(2, 0);
  return (cv::Mat_<double>(3, 3) << 0, -vz, vy, vz, 0, -vx, -vy, vx, 0);
}

// 半四元数 → 3x3 旋转矩阵
static cv::Mat quatMinimal2rot(const cv::Mat& q) {
  cv::Mat p = q.t() * q;
  double w = sqrt(1 - p.at<double>(0, 0));
  cv::Mat diagP = cv::Mat::eye(3, 3, CV_64FC1) * p.at<double>(0, 0);
  return 2 * q * q.t() + 2 * w * skew(q) + cv::Mat::eye(3, 3, CV_64FC1) - 2 * diagP;
}

static void calibrateHandEyeTsai(const std::vector<cv::Mat>& Hg, const std::vector<cv::Mat>& Hc,
                                 cv::Mat& R_cam2gripper, cv::Mat& t_cam2gripper) {
  // 姿态两两组合数
  int K = static_cast<int>((Hg.size() * Hg.size() - Hg.size()) / 2.0);
  cv::Mat A(3 * K, 3, CV_64FC1);
  cv::Mat B(3 * K, 1, CV_64FC1);
  std::vector<cv::Mat> vecHgij, vecHcij;
  vecHgij.reserve((size_t)K);
  vecHcij.reserve((size_t)K);
  int idx = 0;
  for (size_t i = 0; i < Hg.size(); i++) {
    for (size_t j = i + 1; j < Hg.size(); j++, idx++) {
      // Hgij: 追踪器 Gi→Gj, Hcij: 标定板 Ci→Cj
      cv::Mat Hgij = calibHomogeneousInverse(Hg[j]) * Hg[i];
      vecHgij.push_back(Hgij);
      cv::Mat Pgij = 2 * rot2quatMinimal(Hgij);
      cv::Mat Hcij = Hc[j] * calibHomogeneousInverse(Hc[i]);
      vecHcij.push_back(Hcij);
      cv::Mat Pcij = 2 * rot2quatMinimal(Hcij);
      // 旋转: skew(Pgij+Pcij) * Pcg = Pcij - Pgij (不用位移, 不受缩放影响)
      skew(Pgij + Pcij).copyTo(A(cv::Rect(0, idx * 3, 3, 3)));
      cv::Mat diff = Pcij - Pgij;
      diff.copyTo(B(cv::Rect(0, idx * 3, 1, 3)));
    }
  }
  cv::Mat Pcg;
  cv::solve(A, B, Pcg, cv::DECOMP_SVD);
  cv::Mat PcgNorm = Pcg.t() * Pcg;
  cv::Mat PcgU = 2 * Pcg / sqrt(1 + PcgNorm.at<double>(0, 0));
  cv::Mat Rcg = quatMinimal2rot(PcgU / 2.0);
  // 位移: (Rgij - I | -Rcg*Tcij) * [Tcg, Scale]^T = -Tgij (scale 列改进)
  cv::Mat TA(3 * K, 4, CV_64FC1);
  cv::Mat TB(3 * K, 1, CV_64FC1);
  idx = 0;
  for (size_t i = 0; i < Hg.size(); i++) {
    for (size_t j = i + 1; j < Hg.size(); j++, idx++) {
      cv::Mat Hgij = vecHgij[(size_t)idx];
      cv::Mat Hcij = vecHcij[(size_t)idx];
      cv::Mat ldiff1 = Hgij(cv::Rect(0, 0, 3, 3)) - cv::Mat::eye(3, 3, CV_64FC1);
      ldiff1.copyTo(TA(cv::Rect(0, idx * 3, 3, 3)));
      cv::Mat ldiff2 = -(Rcg * Hcij(cv::Rect(3, 0, 1, 3)));
      ldiff2.copyTo(TA(cv::Rect(3, idx * 3, 1, 3)));
      cv::Mat diff = -Hgij(cv::Rect(3, 0, 1, 3));
      diff.copyTo(TB(cv::Rect(0, idx * 3, 1, 3)));
    }
  }
  cv::Mat Tcg;
  cv::solve(TA, TB, Tcg, cv::DECOMP_SVD);
  R_cam2gripper = Rcg;
  t_cam2gripper = Tcg;
}

void calibrateHandEyeScale(cv::InputArrayOfArrays R_gripper2base,
                           cv::InputArrayOfArrays t_gripper2base,
                           cv::InputArrayOfArrays R_target2cam,
                           cv::InputArrayOfArrays t_target2cam,
                           cv::OutputArray R_cam2gripper, cv::OutputArray t_cam2gripper) {
  std::vector<cv::Mat> Rg, tg, Rt, tt;
  R_gripper2base.getMatVector(Rg);
  t_gripper2base.getMatVector(tg);
  R_target2cam.getMatVector(Rt);
  t_target2cam.getMatVector(tt);
  // Notation: Hg = gripper(追踪器)→base(追踪空间), Hc = target(标定板)→camera
  std::vector<cv::Mat> Hg;
  Hg.reserve(Rg.size());
  for (size_t i = 0; i < Rg.size(); i++) {
    cv::Mat m = cv::Mat::eye(4, 4, CV_64FC1);
    cv::Mat R = m(cv::Rect(0, 0, 3, 3));
    if (Rg[i].size() == cv::Size(3, 3)) {
      Rg[i].convertTo(R, CV_64F);
    } else {
      cv::Rodrigues(Rg[i], R);
    }
    cv::Mat t = m(cv::Rect(3, 0, 1, 3));
    tg[i].convertTo(t, CV_64F);
    Hg.push_back(m);
  }
  std::vector<cv::Mat> Hc;
  Hc.reserve(Rt.size());
  for (size_t i = 0; i < Rt.size(); i++) {
    cv::Mat m = cv::Mat::eye(4, 4, CV_64FC1);
    cv::Mat R = m(cv::Rect(0, 0, 3, 3));
    if (Rt[i].size() == cv::Size(3, 3)) {
      Rt[i].convertTo(R, CV_64F);
    } else {
      cv::Rodrigues(Rt[i], R);
    }
    cv::Mat t = m(cv::Rect(3, 0, 1, 3));
    tt[i].convertTo(t, CV_64F);
    Hc.push_back(m);
  }
  cv::Mat Rcg = cv::Mat::eye(3, 3, CV_64FC1);
  cv::Mat Tcg = cv::Mat::zeros(4, 1, CV_64FC1);
  calibrateHandEyeTsai(Hg, Hc, Rcg, Tcg);
  Rcg.copyTo(R_cam2gripper);
  Tcg.copyTo(t_cam2gripper);
}

cv::Point2f calibCompareOffset(const std::vector<cv::Point2f>& truthPoints,
                               const std::vector<cv::Point2f>& computePoints) {
  cv::Point2f offset = {};
  size_t count = std::min(truthPoints.size(), computePoints.size());
  if (count <= 0) {
    return offset;
  }
  double total = 0.0;
  double maxOffset = 0.0;
  for (size_t i = 0; i < count; i++) {
    double dx = truthPoints[i].x - computePoints[i].x;
    double dy = truthPoints[i].y - computePoints[i].y;
    double dist = sqrt(dx * dx + dy * dy);
    total += dist;
    maxOffset = std::max(maxOffset, dist);
  }
  offset.x = (float)(total / count);
  offset.y = (float)maxOffset;
  return offset;
}

double calibCompareSSIM(const cv::Mat& src, const cv::Mat& dest) {
  if (src.empty() || dest.empty() || src.size() != dest.size() || src.type() != dest.type()) {
    return 0.0;
  }
  cv::Mat a, b;
  src.convertTo(a, CV_64F);
  dest.convertTo(b, CV_64F);
  // 标准 SSIM: 高斯加权均值/方差/协方差, C1/C2 防 0 除 (8bit 动态 L=255)
  const double C1 = 6.5025, C2 = 58.5225;
  cv::Mat mu1, mu2, mu1Mu1, mu2Mu2, mu1Mu2;
  cv::GaussianBlur(a, mu1, cv::Size(11, 11), 1.5);
  cv::GaussianBlur(b, mu2, cv::Size(11, 11), 1.5);
  cv::multiply(mu1, mu1, mu1Mu1);
  cv::multiply(mu2, mu2, mu2Mu2);
  cv::multiply(mu1, mu2, mu1Mu2);
  cv::Mat sigma1Sq, sigma2Sq, sigma12;
  cv::GaussianBlur(cv::Mat(a.mul(a)), sigma1Sq, cv::Size(11, 11), 1.5);
  sigma1Sq -= mu1Mu1;
  cv::GaussianBlur(cv::Mat(b.mul(b)), sigma2Sq, cv::Size(11, 11), 1.5);
  sigma2Sq -= mu2Mu2;
  cv::GaussianBlur(cv::Mat(a.mul(b)), sigma12, cv::Size(11, 11), 1.5);
  sigma12 -= mu1Mu2;
  cv::Mat numer = (2 * mu1Mu2 + C1).mul(2 * sigma12 + C2);
  cv::Mat denom = (mu1Mu1 + mu2Mu2 + C1).mul(sigma1Sq + sigma2Sq + C2);
  cv::Mat ssimMap;
  cv::divide(numer, denom, ssimMap);
  return cv::mean(ssimMap)[0];
}

}  // namespace avox
