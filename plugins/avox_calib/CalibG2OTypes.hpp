#pragma once

// g2o 图优化公共件 (移植自 aoce_g2o/Aoceg2oHelper)
// 约定: 观测 UV 归一化到 0-1 (比像素值收敛更细), 内参顶点优化归一化 fx/fy/cx/cy;
// 每条边必须 setInformation, 否则不收敛。
// avox Mat4x4d 行主序(平移 row3) ↔ Eigen 列主序互转即转置。
// g2o 未启用时整个文件编译为空 (插件自动降级 M1 OpenCV 路径)。

#ifdef AVOX_ENABLE_G2O

#include <Eigen/Dense>
#include <g2o/core/base_binary_edge.h>
#include <g2o/core/base_multi_edge.h>
#include <g2o/core/base_unary_edge.h>
#include <g2o/core/base_vertex.h>
#include <g2o/core/eigen_types.h>
#include <g2o/types/slam3d/vertex_se3.h>

#include "avox/AvoxCalib.h"

namespace avox {
namespace calibg2o {

using Vector5 = Eigen::Matrix<double, 5, 1>;

// avox Mat4x4d (行主序) → Eigen 4x4 (转置)
Eigen::Matrix4d toEigenMat(const Mat4x4d& transform);
// Eigen 4x4 → avox Mat4x4d (转置)
Mat4x4d fromEigenMat(const Eigen::Matrix4d& mat);
// avox Mat4x4d → Eigen 位姿
Eigen::Isometry3d toIsometry(const Mat4x4d& transform);

// 归一化镜头投影: (x/z,y/z) → 畸变 → fx*x+cx (fx/cx 为归一化值, 输出在 0-1 UV 域)
Eigen::Vector2d lensMap(const LensModel& lensModel, const Eigen::Vector3d& pos);

// 重投影误差 (像素): (RMS 均值, 最大)
Eigen::Vector2d projectOffset(const Mat4x4d& target2camera, const LensModel& lensModel,
                              const PointCorners& pointCorners);

// 尺度顶点 (追踪器位移缩放, 加法更新)
class ScaleVertex : public g2o::BaseVertex<1, double> {
 public:
  ScaleVertex() { setToOriginImpl(); }
  void setToOriginImpl() override { _estimate = 1.0; }
  void oplusImpl(const double* update) override { _estimate += update[0]; }
  bool read(std::istream& is) override {
    is >> _estimate;
    return true;
  }
  bool write(std::ostream& os) const override {
    os << _estimate;
    return true;
  }
};

// 内参顶点 (fx,fy,cx,cy 归一化值)
class IntrinsicsVertex : public g2o::BaseVertex<4, Eigen::Vector4d> {
 public:
  IntrinsicsVertex() { setToOriginImpl(); }
  void setToOriginImpl() override { _estimate << 1.0, 1.0, 0.5, 0.5; }
  void oplusImpl(const double* update) override { _estimate += Eigen::Vector4d(update); }
  bool read(std::istream& is) override {
    is >> _estimate[0] >> _estimate[1] >> _estimate[2] >> _estimate[3];
    return true;
  }
  bool write(std::ostream& os) const override {
    os << _estimate[0] << " " << _estimate[1] << " " << _estimate[2] << " " << _estimate[3];
    return true;
  }
};

// 畸变顶点 (k1,k2,p1,p2,k3)
class DistortionVertex : public g2o::BaseVertex<5, Vector5> {
 public:
  DistortionVertex() { setToOriginImpl(); }
  void setToOriginImpl() override { _estimate << 0.0, 0.0, 0.0, 0.0, 0.0; }
  void oplusImpl(const double* update) override { _estimate += Vector5(update); }
  bool read(std::istream& is) override {
    is >> _estimate[0] >> _estimate[1] >> _estimate[2] >> _estimate[3] >> _estimate[4];
    return true;
  }
  bool write(std::ostream& os) const override {
    os << _estimate[0] << " " << _estimate[1] << " " << _estimate[2] << " " << _estimate[3]
       << " " << _estimate[4];
    return true;
  }
};

// 内参/畸变顶点 ↔ LensModel (顶点存归一化值, 与 avox LensModel 同构)
void getLensModel(LensModel& lensModel, const IntrinsicsVertex* intrinsics,
                  const DistortionVertex* distortion);
void setLensModel(const LensModel& lensModel, IntrinsicsVertex* intrinsics,
                  DistortionVertex* distortion);

// 镜头参数 (g2o Parameter, 供重投影边共享, 顶点存归一化值)
class LensModelParameter : public g2o::Parameter {
 public:
  LensModel lensModel;
  Eigen::Vector2d uvScale = Eigen::Vector2d(1.0, 1.0);

  void setLensModel(const LensModel& model) { lensModel = model; }
  Eigen::Vector2d map(const Eigen::Vector3d& pos) const {
    return lensMap(lensModel, pos).cwiseProduct(uvScale);
  }
  bool read(std::istream& is) override {
    is >> lensModel.imageSize.x >> lensModel.imageSize.y >> lensModel.focalCenter.x >>
        lensModel.focalCenter.y >> lensModel.focalLength.x >> lensModel.focalLength.y >>
        lensModel.k1 >> lensModel.k2 >> lensModel.p1 >> lensModel.p2 >> lensModel.k3;
    return true;
  }
  bool write(std::ostream& os) const override {
    os << lensModel.imageSize.x << " " << lensModel.imageSize.y << " " << lensModel.focalCenter.x
       << " " << lensModel.focalCenter.y << " " << lensModel.focalLength.x << " "
       << lensModel.focalLength.y << " " << lensModel.k1 << " " << lensModel.k2 << " "
       << lensModel.p1 << " " << lensModel.p2 << " " << lensModel.k3;
    return true;
  }
};

// 手眼重投影边: 单顶点(target2camera), 误差 = 归一化观测UV - lensMap(target2camera*point)
class ProjectionHandEdge : public g2o::BaseUnaryEdge<2, Eigen::Vector2d, g2o::VertexSE3> {
 public:
  ProjectionHandEdge() {
    resizeParameters(1);
    installParameter(lensModelPar, 0);
  }
  Eigen::Vector3d point;

  void computeError() override {
    const g2o::VertexSE3* t2c = static_cast<const g2o::VertexSE3*>(_vertices[0]);
    Eigen::Vector3d cameraPos = t2c->estimate() * point;
    _error = _measurement - lensModelPar->map(cameraPos);
  }
  bool read(std::istream&) override { return false; }
  bool write(std::ostream&) const override { return false; }

 private:
  LensModelParameter* lensModelPar = nullptr;
};

// 内参BA重投影边: 三顶点(内参,畸变,target2camera), 归一化 UV 域
class CalibrationEdge : public g2o::BaseMultiEdge<2, Eigen::Vector2d> {
 public:
  CalibrationEdge() { resize(3); }
  Eigen::Vector3d point;

  void computeError() override;
  bool read(std::istream&) override { return false; }
  bool write(std::ostream&) const override { return false; }
};

}  // namespace calibg2o
}  // namespace avox

#endif  // AVOX_ENABLE_G2O
