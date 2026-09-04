#ifdef AVOX_ENABLE_G2O
#include "CalibG2OOptimizers.hpp"

#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_gauss_newton.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/core/sparse_optimizer.h>
#include <g2o/solvers/dense/linear_solver_dense.h>

#include "avox/module/LogHelper.hpp"

namespace avox {

namespace calibg2o {

Eigen::Matrix4d toEigenMat(const Mat4x4d& transform) {
  Eigen::Matrix4d mat = Eigen::Matrix4d::Identity();
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      mat(i, j) = (&transform.row0.x)[j * 4 + i];
    }
  }
  return mat;
}

Mat4x4d fromEigenMat(const Eigen::Matrix4d& mat) {
  Mat4x4d transform;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      (&transform.row0.x)[i * 4 + j] = mat(j, i);
    }
  }
  return transform;
}

Eigen::Isometry3d toIsometry(const Mat4x4d& transform) {
  Eigen::Isometry3d iso = Eigen::Isometry3d::Identity();
  iso.matrix() = toEigenMat(transform);
  return iso;
}

Eigen::Vector2d lensMap(const LensModel& lensModel, const Eigen::Vector3d& pos) {
  double x = pos[0] / pos[2];
  double y = pos[1] / pos[2];
  double fx = lensModel.focalLength.x;
  double fy = lensModel.focalLength.y;
  double cx = lensModel.focalCenter.x;
  double cy = lensModel.focalCenter.y;
  double k1 = lensModel.k1, k2 = lensModel.k2, k3 = lensModel.k3;
  double p1 = lensModel.p1, p2 = lensModel.p2;
  double r2 = x * x + y * y;
  double r4 = r2 * r2;
  double r6 = r4 * r2;
  // 切向畸变
  double a1 = 2 * x * y;
  double a2 = r2 + 2 * x * x;
  double a3 = r2 + 2 * y * y;
  double cdist = 1.0 + k1 * r2 + k2 * r4 + k3 * r6;
  x = x * cdist + p1 * a1 + p2 * a2;
  y = y * cdist + p1 * a3 + p2 * a1;
  return Eigen::Vector2d(fx * x + cx, fy * y + cy);
}

Eigen::Vector2d projectOffset(const Mat4x4d& target2camera, const LensModel& lensModel,
                              const PointCorners& pointCorners) {
  Eigen::Isometry3d cornersPose = toIsometry(target2camera);
  Eigen::Vector2d imageSize(pointCorners.imageSize.x, pointCorners.imageSize.y);
  double totalOffset = 0.0;
  double maxOffset = 0.0;
  for (int32_t i = 0; i < pointCorners.count; i++) {
    Eigen::Vector3d point(pointCorners.points[i].x, pointCorners.points[i].y,
                          pointCorners.points[i].z);
    Eigen::Vector2d corner(pointCorners.corners[i].x / imageSize[0],
                           pointCorners.corners[i].y / imageSize[1]);
    // 归一化 UV 域
    Eigen::Vector2d map = lensMap(lensModel, cornersPose * point);
    Eigen::Vector2d distance = corner - map;
    double length = distance.dot(distance);
    if (maxOffset < length) {
      maxOffset = length;
    }
    totalOffset += length;
  }
  int32_t count = std::max(1, pointCorners.count);
  return Eigen::Vector2d(std::sqrt(totalOffset / count), std::sqrt(maxOffset));
}

void getLensModel(LensModel& lensModel, const IntrinsicsVertex* intrinsics,
                  const DistortionVertex* distortion) {
  lensModel.focalLength.x = intrinsics->estimate()[0];
  lensModel.focalLength.y = intrinsics->estimate()[1];
  lensModel.focalCenter.x = intrinsics->estimate()[2];
  lensModel.focalCenter.y = intrinsics->estimate()[3];
  lensModel.k1 = distortion->estimate()[0];
  lensModel.k2 = distortion->estimate()[1];
  lensModel.p1 = distortion->estimate()[2];
  lensModel.p2 = distortion->estimate()[3];
  lensModel.k3 = distortion->estimate()[4];
}

void setLensModel(const LensModel& lensModel, IntrinsicsVertex* intrinsics,
                  DistortionVertex* distortion) {
  if (intrinsics) {
    intrinsics->setEstimate(Eigen::Vector4d(lensModel.focalLength.x, lensModel.focalLength.y,
                                            lensModel.focalCenter.x, lensModel.focalCenter.y));
  }
  if (distortion) {
    Vector5 dist;
    dist << lensModel.k1, lensModel.k2, lensModel.p1, lensModel.p2, lensModel.k3;
    distortion->setEstimate(dist);
  }
}

}  // namespace calibg2o

using namespace calibg2o;

// ============ 边 ============

void CalibHandEyeEdge::computeError() {
  const g2o::VertexSE3* camera2trackVec = static_cast<const g2o::VertexSE3*>(_vertices[0]);
  const g2o::VertexSE3* base2targetVec = static_cast<const g2o::VertexSE3*>(_vertices[1]);
  const g2o::VertexSE3* target2cameraVec = static_cast<const g2o::VertexSE3*>(_vertices[2]);
  const ScaleVertex* scaleVec = static_cast<const ScaleVertex*>(_vertices[3]);
  Eigen::Isometry3d camera2track = camera2trackVec->estimate();
  Eigen::Isometry3d base2target = base2targetVec->estimate();
  Eigen::Isometry3d target2camera = target2cameraVec->estimate();
  double scale = scaleVec->estimate();
  // 优化量反推的 base2track: camera2track · target2camera · base2target
  Eigen::Isometry3d base2track = camera2track * target2camera * base2target;
  // 实测 track2base 平移转真实尺度
  Eigen::Isometry3d measurementScale = _measurement;
  measurementScale.translation() = scale * _measurement.translation();
  // 6 维李群误差 (旋转+平移)
  Eigen::Isometry3d delta = measurementScale * base2track;
  _error = g2o::internal::toVectorMQT(delta);
}

// ============ CalibHandEyeOptimizer ============

void CalibHandEyeOptimizer::fillData(int32_t count, const TrackCorners* dataPtr) {
  trackCorners.clear();
  for (int32_t i = 0; i < count; i++) {
    trackCorners.push_back(*(dataPtr + i));
  }
}

CameraTrack CalibHandEyeOptimizer::compute(const HandEyeParamet& paramet,
                                           const LensModel& lensModel,
                                           const CameraTrack& cameraTrack) {
  g2o::SparseOptimizer optimizer;
  using LinearSolver = g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>;
  g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(
      std::make_unique<g2o::BlockSolverX>(std::make_unique<LinearSolver>()));
  optimizer.setAlgorithm(solver);
  // 镜头参数 (归一化内参, 边共享)
  LensModelParameter* lensPar = new LensModelParameter();
  lensPar->lensModel = lensModel;
  lensPar->setId(0);
  optimizer.addParameter(lensPar);
  // 顶点: camera2track / base2target / scale
  g2o::VertexSE3* c2tVec = new g2o::VertexSE3();
  c2tVec->setEstimate(toIsometry(cameraTrack.camera2track));
  c2tVec->setId(0);
  optimizer.addVertex(c2tVec);
  g2o::VertexSE3* b2tVec = new g2o::VertexSE3();
  b2tVec->setEstimate(toIsometry(cameraTrack.base2target));
  b2tVec->setId(1);
  optimizer.addVertex(b2tVec);
  ScaleVertex* scaleVec = new ScaleVertex();
  scaleVec->setEstimate(cameraTrack.scale);
  scaleVec->setId(2);
  scaleVec->setFixed(paramet.bFixScale);
  optimizer.addVertex(scaleVec);
  // 手眼边信息矩阵: 平移块 x0.01, 旋转块 x1.0
  Eigen::MatrixXd handEyeInf = Eigen::MatrixXd::Identity(6, 6);
  handEyeInf.topLeftCorner(3, 3) *= 0.01;
  // 每条记录: 每帧 target2camera 顶点 + 手眼边 + 逐角点重投影边
  for (const TrackCorners& trackCorner : trackCorners) {
    Eigen::Isometry3d track2base = toIsometry(trackCorner.trackPose);
    // track 平移转真实尺度 (初值 scale)
    Eigen::Isometry3d track2baseScale = track2base;
    track2baseScale.translation() = cameraTrack.scale * track2base.translation();
    // 每帧 target2camera 初值: 有 PnP 位姿用其逆, 否则由 cameraTrack 反推
    Eigen::Isometry3d c2tInit = toIsometry(cameraTrack.camera2track);
    Eigen::Isometry3d b2tInit = toIsometry(cameraTrack.base2target);
    Eigen::Isometry3d camera2target;
    if (trackCorner.cameraPose.valid()) {
      camera2target = toIsometry(trackCorner.cameraPose);
    } else {
      camera2target = Eigen::Isometry3d(track2baseScale * c2tInit * b2tInit);
    }
    g2o::VertexSE3* t2cVec = new g2o::VertexSE3();
    t2cVec->setEstimate(camera2target.inverse());
    t2cVec->setId((int)optimizer.vertices().size());
    optimizer.addVertex(t2cVec);
    // 手眼边: 实测 track2base 与优化量反推比较
    CalibHandEyeEdge* handEyeEdge = new CalibHandEyeEdge();
    handEyeEdge->vertices()[0] = c2tVec;
    handEyeEdge->vertices()[1] = b2tVec;
    handEyeEdge->vertices()[2] = t2cVec;
    handEyeEdge->vertices()[3] = scaleVec;
    handEyeEdge->setMeasurement(track2base);
    handEyeEdge->setInformation(handEyeInf);
    handEyeEdge->setId((int)optimizer.edges().size());
    if (paramet.robustHandEye) {
      g2o::RobustKernelHuber* kernel = new g2o::RobustKernelHuber();
      kernel->setDelta(paramet.handEyeDelta);
      handEyeEdge->setRobustKernel(kernel);
    }
    optimizer.addEdge(handEyeEdge);
    // 重投影边 (归一化 UV 域, 粒度小收敛更细)
    Eigen::Vector2d imageSize(trackCorner.pointCorners.imageSize.x, trackCorner.pointCorners.imageSize.y);
    for (int32_t i = 0; i < trackCorner.pointCorners.count; i++) {
      ProjectionHandEdge* proEdge = new ProjectionHandEdge();
      proEdge->setMeasurement(Eigen::Vector2d(trackCorner.pointCorners.corners[i].x / imageSize[0],
                                              trackCorner.pointCorners.corners[i].y / imageSize[1]));
      proEdge->setInformation(Eigen::Matrix2d::Identity() * 0.01);
      proEdge->vertices()[0] = t2cVec;
      proEdge->setParameterId(0, 0);
      proEdge->setId((int)optimizer.edges().size());
      proEdge->point = Eigen::Vector3d(trackCorner.pointCorners.points[i].x,
                                       trackCorner.pointCorners.points[i].y,
                                       trackCorner.pointCorners.points[i].z);
      if (paramet.projectionHand) {
        // 归一化 UV 域: delta 0.01 ≈ 19px@1920 (aoce 原版 delta=1.0 是像素域量纲)
        g2o::RobustKernelHuber* kernel = new g2o::RobustKernelHuber();
        kernel->setDelta(0.01);
        proEdge->setRobustKernel(kernel);
      }
      optimizer.addEdge(proEdge);
    }
  }
  bool bInit = optimizer.initializeOptimization();
  if (!bInit) {
    lastError = "hand eye optimizer init failed";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return cameraTrack;
  }
  optimizer.optimize(20);
  CameraTrack result;
  result.camera2track = fromEigenMat(c2tVec->estimate().matrix());
  result.base2target = fromEigenMat(b2tVec->estimate().matrix());
  result.scale = scaleVec->estimate();
  return result;
}

const char* CalibHandEyeOptimizer::getLastError() { return lastError.c_str(); }

// ============ CalibCalibrationOptimizer ============

void CalibrationEdge::computeError() {
  const IntrinsicsVertex* intrinsics = static_cast<const IntrinsicsVertex*>(_vertices[0]);
  const DistortionVertex* distortion = static_cast<const DistortionVertex*>(_vertices[1]);
  const g2o::VertexSE3* t2cVec = static_cast<const g2o::VertexSE3*>(_vertices[2]);
  LensModel lensModel;
  getLensModel(lensModel, intrinsics, distortion);
  Eigen::Vector3d cameraPos = t2cVec->estimate() * point;
  _error = _measurement - lensMap(lensModel, cameraPos);
}

void CalibCalibrationOptimizer::fillData(int32_t count, const TrackCorners* dataPtr) {
  trackCorners.clear();
  for (int32_t i = 0; i < count; i++) {
    trackCorners.push_back(*(dataPtr + i));
  }
}

LensModel CalibCalibrationOptimizer::compute(const LensModel& lensModel) {
  g2o::SparseOptimizer optimizer;
  using LinearSolver = g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>;
  g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(
      std::make_unique<g2o::BlockSolverX>(std::make_unique<LinearSolver>()));
  optimizer.setAlgorithm(solver);
  IntrinsicsVertex* intrinsicsVec = new IntrinsicsVertex();
  intrinsicsVec->setId(0);
  optimizer.addVertex(intrinsicsVec);
  DistortionVertex* distortVec = new DistortionVertex();
  distortVec->setId(1);
  optimizer.addVertex(distortVec);
  setLensModel(lensModel, intrinsicsVec, distortVec);
  // 每帧 target2camera 顶点 + 逐角点重投影边
  std::vector<g2o::VertexSE3*> cameraVecs;
  for (const TrackCorners& trackCorner : trackCorners) {
    // cameraPose = camera2target → target2camera
    Eigen::Isometry3d target2camera = toIsometry(trackCorner.cameraPose).inverse();
    g2o::VertexSE3* t2cVec = new g2o::VertexSE3();
    t2cVec->setEstimate(target2camera);
    t2cVec->setId((int)optimizer.vertices().size());
    optimizer.addVertex(t2cVec);
    cameraVecs.push_back(t2cVec);
    Eigen::Vector2d imageSize(trackCorner.pointCorners.imageSize.x, trackCorner.pointCorners.imageSize.y);
    for (int32_t i = 0; i < trackCorner.pointCorners.count; i++) {
      CalibrationEdge* pnpEdge = new CalibrationEdge();
      pnpEdge->setId((int)optimizer.edges().size());
      pnpEdge->setMeasurement(Eigen::Vector2d(trackCorner.pointCorners.corners[i].x / imageSize[0],
                                              trackCorner.pointCorners.corners[i].y / imageSize[1]));
      pnpEdge->setInformation(Eigen::Matrix2d::Identity() * 0.01);
      pnpEdge->vertices()[0] = intrinsicsVec;
      pnpEdge->vertices()[1] = distortVec;
      pnpEdge->vertices()[2] = t2cVec;
      pnpEdge->point = Eigen::Vector3d(trackCorner.pointCorners.points[i].x,
                                       trackCorner.pointCorners.points[i].y,
                                       trackCorner.pointCorners.points[i].z);
      // 归一化 UV 域 Huber delta (≈19px@1920)
      g2o::RobustKernelHuber* kernel = new g2o::RobustKernelHuber();
      kernel->setDelta(0.01);
      pnpEdge->setRobustKernel(kernel);
      optimizer.addEdge(pnpEdge);
    }
  }
  bool bInit = optimizer.initializeOptimization();
  if (!bInit) {
    lastError = "calibration optimizer init failed";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return lensModel;
  }
  optimizer.optimize(100);
  LensModel result = lensModel;
  getLensModel(result, intrinsicsVec, distortVec);
  resultLens = result;
  // 回写每帧优化后的位姿 (camera2target)
  for (size_t i = 0; i < trackCorners.size(); i++) {
    Eigen::Isometry3d c2t = cameraVecs[i]->estimate().inverse();
    trackCorners[i].cameraPose = fromEigenMat(c2t.matrix());
  }
  return result;
}

bool CalibCalibrationOptimizer::getCameraPose(int32_t frameIndex, Mat4x4d& cameraPose) {
  if (frameIndex < 0 || frameIndex >= (int32_t)trackCorners.size()) {
    return false;
  }
  cameraPose = trackCorners[frameIndex].cameraPose;
  return true;
}

float CalibCalibrationOptimizer::getOffset(int32_t frameIndex) {
  if (frameIndex < 0 || frameIndex >= (int32_t)trackCorners.size()) {
    return -1.0f;
  }
  // cameraPose=camera2target → target2camera 投影
  Mat4x4d target2camera = trackCorners[frameIndex].cameraPose.inverse();
  Eigen::Vector2d offset = calibg2o::projectOffset(target2camera, resultLens,
                                                   trackCorners[frameIndex].pointCorners);
  // projectOffset 返回归一化域 (均方/最大), 转像素
  Eigen::Vector2d imageSize(trackCorners[frameIndex].pointCorners.imageSize.x,
                            trackCorners[frameIndex].pointCorners.imageSize.y);
  return (float)(offset[0] * imageSize[0]);
}

const char* CalibCalibrationOptimizer::getLastError() { return lastError.c_str(); }

}  // namespace avox

#endif  // AVOX_ENABLE_G2O
