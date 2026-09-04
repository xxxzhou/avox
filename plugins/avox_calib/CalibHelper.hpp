#pragma once

// 标定数学/格式转换工具 (移植自 aoce_opencv/OpenCVHelper + CalibrateHandEyeScale)
// 约定: avox Mat4x4d 行主序 (旋转 3x3 在 row0-2, 平移在 row3.xyz) ↔
//       cv::Mat 列主序, 互转即转置, 严禁 memcpy。

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <string>
#include <vector>

#include "avox/AvoxCalib.h"
#include "avox/AvoxImage.h"

namespace avox {

// CalibArucoType → cv::aruco::PredefinedDictionaryType
cv::aruco::PredefinedDictionaryType getCvArucoType(CalibArucoType arucoType);

// ============ cv 异常装甲 (同 avox_opencv/OpencvHelper, 本插件自带一份) ============
// 坏帧/边界尺寸会让 cv:: 抛 cv::Exception, 异常穿过绑定层 std::terminate 宿主进程。
// 所有吃 IImageBuffer 的公共入口必须用此宏包住: 异常 → lastError 归因 + 返回失败值。
#define AVOX_CALIB_TRY try {
#define AVOX_CALIB_CATCH_RET(ret)                                                  \
  }                                                                               \
  catch (const cv::Exception &e_) {                                               \
    lastError = std::string("cv exception: ") + e_.what();                        \
    return ret;                                                                   \
  }                                                                               \
  catch (const std::exception &e_) {                                              \
    lastError = std::string("exception: ") + e_.what();                           \
    return ret;                                                                   \
  }                                                                               \
  catch (...) {                                                                   \
    lastError = "unknown exception";                                              \
    return ret;                                                                   \
  }

// ============ 图像转换 ============

// ImageType → cv Mat 类型
int32_t calibGetMatType(const ImageType& imageType);

// IImageBuffer → cv::Mat (深拷贝, rgba8/bgra8 统一转 RGBA 处理)
cv::Mat calibImage2Mat(IImageBuffer* buffer);

// ============ avox Mat4x4d ↔ cv::Mat ============

// Mat4x4d → cv 旋转 3x3 + 平移 3x1 (cv 侧为行主序语义的数学矩阵, 元素转置拷贝)
void calibMat4x42Mat(const Mat4x4d& transform, cv::Mat& rotator, cv::Mat& position);
// cv 旋转 + 平移 → Mat4x4d
void calibMat2Mat4x4(const cv::Mat& rotator, const cv::Mat& position, Mat4x4d& transform);
// cv 旋转 + 平移 → cv 4x4
void calibMat2Mat4x4(const cv::Mat& rotator, const cv::Mat& position, cv::Mat& transform);
// cv 4x4 → Mat4x4d
void calibMat2Mat4x4(const cv::Mat& cvTrans, Mat4x4d& transform);
// Mat4x4d → cv 4x4
void calibMat4x42Mat(const Mat4x4d& transform, cv::Mat& cvTrans);

// ============ 内参转换 ============

// LensModel (归一化) → cv 内参矩阵 + 畸变系数 (5x1: k1,k2,p1,p2,k3)
void calibLensModelToCV(const LensModel& lensModel, cv::Mat& cameraMatrix, cv::Mat& distCoeffs);
// cv 内参 + 畸变 → LensModel (归一化到 imageSize)
void calibLensCVToModel(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
                        const vec2d& imageSize, LensModel& lensModel);

// ============ 位姿数学 ============

// 4x4 齐次矩阵求逆 (旋转正交: R^T / -R^T*t, 比通用求逆稳)
cv::Mat calibHomogeneousInverse(const cv::Mat& T);
cv::Mat calibHomogeneousInverse(const cv::Mat& rotMat, const cv::Mat& trans);
// 近似旋转矩阵正交化 (SVD: U*V^T)
void calibNormalRot(cv::Mat& rotate);

// Tsai 手眼标定 + scale 列改进:
// 传统 Tsai 位移方程 (Rgij-I)*Tcg = Rcg*Tcij - Tgij 要求追踪器与摄像机位移同尺寸;
// 改进为 (Rgij-I | -Rcg*Tcij)·[Tcg, Scale]^T = -Tgij, 一次 SVD 同时解出平移与缩放
// (Redspy 等光学追踪器位移存在未知缩放时必须开启)。
// 输入 t_cam2gripper 为 4x1, 第 4 分量 = Scale, 位移 = 前三分量 * Scale。
void calibrateHandEyeScale(cv::InputArrayOfArrays R_gripper2base,
                           cv::InputArrayOfArrays t_gripper2base,
                           cv::InputArrayOfArrays R_target2cam,
                           cv::InputArrayOfArrays t_target2cam,
                           cv::OutputArray R_cam2gripper, cv::OutputArray t_cam2gripper);

// 重投影误差: truth 与 compute 逐点欧氏距离, 返回 (平均, 最大)
cv::Point2f calibCompareOffset(const std::vector<cv::Point2f>& truthPoints,
                               const std::vector<cv::Point2f>& computePoints);

// SSIM 结构相似度 (0-1, 衡量识别到的 Aruco 与原图像相似 → 模糊度), imgproc 自实现
// (预编译 opencv_world 不含 quality 模块)
double calibCompareSSIM(const cv::Mat& src, const cv::Mat& dest);

}  // namespace avox
