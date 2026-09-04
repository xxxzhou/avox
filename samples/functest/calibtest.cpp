/**
 * @file calibtest.cpp
 * @brief 虚拟制片相机标定端到端测试 (avox_calib 插件)
 *
 * 合成数据回归: 给定真值内参/手眼(camera2track)/base2target/位移缩放,
 * 渲染 N 个位姿下的棋盘格图像 + 换算追踪器位姿, 全流程标定后与真值比对。
 * (aoce 开发期同款验证法, 不依赖真实设备/数据)
 *
 * 用法: calibtest   (无参自测)
 *
 * 坐标约定: 算法侧统一 OpenCV 系 (x右 y下 z前); 追踪器位姿输入前需转到 OpenCV 系
 * (真实场景用 convertUE4ToOpenCV)。测试内部用 cv 约定 4x4 (列向量 M·v, 平移在第4列),
 * 边界处转置写入 avox Mat4x4d (avox 行主序行向量约定, 平移在 row3)。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "avox/AvoxCalib.h"
#include "avox/AvoxImage.h"
#include "avox/module/AvoxManager.hpp"

using namespace avox;

// ============ cv 约定 4x4 工具 (行主序存储, 列向量语义 M·v, 平移在第 4 列) ============

struct M4 {
  double m[4][4] = {};

  static M4 ident() {
    M4 r;
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0;
    return r;
  }
  static M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        for (int k = 0; k < 4; k++) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
  }
  // 刚体求逆: R^T / -R^T t
  static M4 rigidInverse(const M4& a) {
    M4 r = ident();
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) r.m[i][j] = a.m[j][i];
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) r.m[i][3] -= r.m[i][j] * a.m[j][3];
    return r;
  }
  static M4 makeRT(const double R[3][3], const double t[3]) {
    M4 r = ident();
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) r.m[i][j] = R[i][j];
    for (int i = 0; i < 3; i++) r.m[i][3] = t[i];
    return r;
  }
  void getR(double R[3][3]) const {
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) R[i][j] = m[i][j];
  }
  void getT(double t[3]) const {
    for (int i = 0; i < 3; i++) t[i] = m[i][3];
  }
};

// cv 约定矩阵 → avox Mat4x4d (avox 行主序行向量 = cv 转置, 平移在 row3)
static Mat4x4d toAvox(const M4& c) {
  Mat4x4d r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) (&r.row0.x)[i * 4 + j] = c.m[j][i];
  return r;
}

// 旋转: R = Rz(z)·Ry(y)·Rx(x) (弧度)
static void eulerToR(double rx, double ry, double rz, double R[3][3]) {
  double cx = cos(rx), sx = sin(rx), cy = cos(ry), sy = sin(ry), cz = cos(rz), sz = sin(rz);
  double Rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
  double Ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
  double Rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
  // Rz·Ry·Rx
  double RyRx[3][3] = {};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) RyRx[i][j] += Ry[i][k] * Rx[k][j];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      R[i][j] = 0;
      for (int k = 0; k < 3; k++) R[i][j] += Rz[i][k] * RyRx[k][j];
    }
}

// 旋转角度差 (度): θ = acos((trace(R1ᵀ·R2)-1)/2)
static double rotDiffDeg(const M4& a, const M4& b) {
  // (R1ᵀ·R2)[i][i] = Σ_j a.m[j][i]·b.m[j][i]
  double tr = 0.0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) tr += a.m[j][i] * b.m[j][i];
  double c = (tr - 1.0) / 2.0;
  c = std::max(-1.0, std::min(1.0, c));
  return acos(c) * 180.0 / 3.14159265358979323846;
}

// ============ 合成图像渲染 (棋盘格投影绘制) ============

static const int32_t kImgW = 1280;
static const int32_t kImgH = 720;
// 真值内参 (无畸变): fx=fy=900, 中心 (640,360)
static const double kFx = 900.0, kFy = 900.0, kCx = 640.0, kCy = 360.0;
// 棋盘格: 9x6 内角点, 格子 0.03m
static const int32_t kBoardW = 9, kBoardH = 6;
static const double kChessSize = 0.03;

struct Vec3 {
  double x, y, z;
};

// 3D 点经 target2camera 投影到像素
static Vec3 projectPoint(const M4& target2camera, const Vec3& p, double& u, double& v) {
  double R[3][3], t[3];
  target2camera.getR(R);
  target2camera.getT(t);
  double pc[3] = {R[0][0] * p.x + R[0][1] * p.y + R[0][2] * p.z + t[0],
                  R[1][0] * p.x + R[1][1] * p.y + R[1][2] * p.z + t[1],
                  R[2][0] * p.x + R[2][1] * p.y + R[2][2] * p.z + t[2]};
  u = kFx * pc[0] / pc[2] + kCx;
  v = kFy * pc[1] / pc[2] + kCy;
  return {pc[0], pc[1], pc[2]};
}

// 填充三角形 (边缘函数)
static void fillTriangle(uint8_t* img, int32_t w, int32_t h, double x0, double y0, double x1,
                         double y1, double x2, double y2, uint8_t gray) {
  double minX = std::max(0.0, floor(std::min({x0, x1, x2})));
  double maxX = std::min((double)w - 1, ceil(std::max({x0, x1, x2})));
  double minY = std::max(0.0, floor(std::min({y0, y1, y2})));
  double maxY = std::min((double)h - 1, ceil(std::max({y0, y1, y2})));
  auto edge = [](double ax, double ay, double bx, double by, double px, double py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  };
  for (int32_t y = (int32_t)minY; y <= (int32_t)maxY; y++) {
    for (int32_t x = (int32_t)minX; x <= (int32_t)maxX; x++) {
      double px = x + 0.5, py = y + 0.5;
      double w0 = edge(x0, y0, x1, y1, px, py);
      double w1 = edge(x1, y1, x2, y2, px, py);
      double w2 = edge(x2, y2, x0, y0, px, py);
      bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (inside) {
        uint8_t* p = img + (y * w + x) * 4;
        p[0] = p[1] = p[2] = gray;
        p[3] = 255;
      }
    }
  }
}

// 合成目标微非平面扰动 (1.5cm 级): 完全平面+无噪声是 PnP 二义性病态数据,
// 4.13 的 solvePnP 可能收敛到重投影同样好的错误折叠位姿, 真实标定物不存在此问题
static double boardZ(int32_t i, int32_t j) {
  return 0.015 * sin(1.7 * i + 0.9 * j);
}

// 渲染一帧: 白底 + 黑白交替棋盘格 (投影正对图像时 3D 点序与识别序一致)
static void renderChessboard(const M4& target2camera, uint8_t* img) {
  for (int32_t i = 0; i < kImgW * kImgH * 4; i += 4) {
    img[i] = img[i + 1] = img[i + 2] = 255;
    img[i + 3] = 255;
  }
  // 内角点 9x6 → 格子 10x7 → 网格顶点 11x8
  int32_t vx = kBoardW + 2, vy = kBoardH + 2;
  std::vector<Vec3> corners3d((size_t)(vx * vy));
  std::vector<double> us((size_t)(vx * vy)), vs((size_t)(vx * vy));
  for (int32_t j = 0; j < vy; j++) {
    for (int32_t i = 0; i < vx; i++) {
      Vec3 p = {kChessSize * i, kChessSize * j, boardZ(i, j)};
      size_t idx = (size_t)j * vx + i;
      corners3d[idx] = p;
      projectPoint(target2camera, p, us[idx], vs[idx]);
    }
  }
  for (int32_t j = 0; j < vy - 1; j++) {
    for (int32_t i = 0; i < vx - 1; i++) {
      if ((i + j) % 2 != 0) continue;  // 交替填黑
      size_t idx = (size_t)j * vx + i;
      double x0 = us[idx], y0 = vs[idx];
      double x1 = us[idx + 1], y1 = vs[idx + 1];
      double x2 = us[idx + vx], y2 = vs[idx + vx];
      double x3 = us[idx + vx + 1], y3 = vs[idx + vx + 1];
      fillTriangle(img, kImgW, kImgH, x0, y0, x1, y1, x2, y2, 0);
      fillTriangle(img, kImgW, kImgH, x1, y1, x3, y3, x2, y2, 0);
    }
  }
}

// ============ 合成数据 ============

static const int32_t kPoseCount = 8;
static const double kGtScale = 0.1;  // 真值缩放: 追踪器报告位移 = 真实位移 / 0.1

struct SynthPose {
  M4 target2camera;   // 标定板 → 摄像机 (cv)
  M4 track2baseReal;  // 追踪器 → 追踪空间 (真实米制, cv)
};

// 确定性伪随机
static double prand(int32_t seed) {
  double x = sin((double)seed * 12.9898) * 43758.5453;
  return x - floor(x);
}

// 追踪器报告位姿: 旋转不变 (尺度无关), 平移换成追踪器单位 (真实位移/scale)
static M4 scaleTrackTranslation(const M4& real, double scale) {
  M4 r = real;
  for (int i = 0; i < 3; i++) r.m[i][3] = real.m[i][3] / scale;
  return r;
}

static std::vector<SynthPose> buildSyntheticPoses(const M4& camera2track, const M4& base2target) {
  std::vector<SynthPose> poses;
  for (int32_t i = 0; i < kPoseCount; i++) {
    // 标定板摆在相机前 0.7~1.2m, 姿态受限旋转 (保持棋盘行列序与图像序一致)
    double yaw = (prand(i * 4 + 1) - 0.5) * 0.62;     // ±18°
    double pitch = (prand(i * 4 + 2) - 0.5) * 0.52;   // ±15°
    double roll = (prand(i * 4 + 3) - 0.5) * 0.42;    // ±12°
    double dist = 0.7 + prand(i * 4 + 4) * 0.5;
    double tx = (prand(i * 7 + 5) - 0.5) * 0.2;
    double ty = (prand(i * 7 + 6) - 0.5) * 0.15;
    double R[3][3];
    eulerToR(pitch, yaw, roll, R);
    double t[3] = {tx, ty, dist};
    SynthPose pose;
    pose.target2camera = M4::makeRT(R, t);
    // track2base = base2target⁻¹ · target2camera⁻¹(=camera2target) · camera2track⁻¹
    M4 camera2target = M4::rigidInverse(pose.target2camera);
    pose.track2baseReal = M4::mul(M4::mul(M4::rigidInverse(base2target), camera2target),
                                  M4::rigidInverse(camera2track));
    poses.push_back(pose);
  }
  return poses;
}

// ============ 校验输出 ============

static int32_t failCount = 0;

static void check(bool ok, const char* name, const char* detail = "") {
  printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failCount++;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);  // 崩溃定位: 无缓冲输出
  printf("=== 虚拟制片相机标定端到端测试 (avox_calib) ===\n");
  // 真值
  double gtXyz[3] = {0.05, -0.03, 0.12};  // camera2track 平移 (米)
  double Rct[3][3];
  eulerToR(0.05, -0.08, 0.03, Rct);
  M4 gtCamera2track = M4::makeRT(Rct, gtXyz);
  double Rbt[3][3];
  eulerToR(0.2, 0.4, -0.15, Rbt);
  double btT[3] = {1.2, 0.5, -0.3};
  M4 gtBase2target = M4::makeRT(Rbt, btT);
  std::vector<SynthPose> poses = buildSyntheticPoses(gtCamera2track, gtBase2target);
  printf("真值: scale=%.2f camera2track t=(%.2f,%.2f,%.2f)m\n", kGtScale, gtXyz[0], gtXyz[1],
         gtXyz[2]);

  // 从 hub 创建 (create 内部加载 avox_calib 插件)
  std::unique_ptr<IImagePoints> points(AvoxManager::Get().imagePointsHub.create("opencv"));
  std::unique_ptr<ICameraCalibration> calibration(
      AvoxManager::Get().cameraCalibrationHub.create("opencv"));
  std::unique_ptr<ICameraOffset> offset(AvoxManager::Get().cameraOffsetHub.create("opencv"));
  std::unique_ptr<IPnpCameraPose> pnp(AvoxManager::Get().pnpCameraPoseHub.create("opencv"));
  std::unique_ptr<IVideoCalibration> video(AvoxManager::Get().videoCalibrationHub.create("opencv"));
  if (!points || !calibration || !offset || !pnp || !video) {
    printf("FAIL 创建标定对象失败 (avox_calib 插件未加载?)\n");
    return 1;
  }
  ChessboardInfo chess = {kBoardW, kBoardH, (float)kChessSize};
  points->setChessboardInfo(chess);
  // 覆盖为微非平面点集: 内角点(i,j)位于渲染顶点网格 (i+1,j+1), x/y/z 必须与渲染
  // 完全一致, 否则构造出畸变曲面 (消平面 PnP 二义性)
  {
    std::vector<vec3f> pts;
    for (int32_t j = 0; j < kBoardH; j++)
      for (int32_t i = 0; i < kBoardW; i++)
        pts.push_back(vec3f((float)(kChessSize * (i + 1)), (float)(kChessSize * (j + 1)),
                            (float)boardZ(i + 1, j + 1)));
    points->setPoints((int32_t)pts.size(), pts.data());
  }
  LensModel gtLens = getLensModelFromFov(2.0 * atan((double)kImgH * 0.5 / kFy), kImgW, kImgH);

  // ============ 1. 内参标定 ============
  printf("\n[1] 内参标定 (N=%d 张合成棋盘格)\n", kPoseCount);
  vec2d imgSize = vec2d((double)kImgW, (double)kImgH);
  calibration->setImagePoints(imgSize, points.get());
  std::vector<std::unique_ptr<IImageBuffer>> images;
  for (int32_t i = 0; i < kPoseCount; i++) {
    images.emplace_back(createImageBuffer());
    ImageFormat fmt;
    fmt.width = kImgW;
    fmt.height = kImgH;
    fmt.imageType = ImageType::rgba8;
    images.back()->setImageFormat(fmt);
    renderChessboard(poses[i].target2camera, images.back()->getPointer());
    if (i == 0) saveImagePath("calib_test_frame0.png", images.back().get());  // 调试落盘
    int32_t n = calibration->saveCornerImage(images.back().get());
    if (n < 0) {
      printf("FAIL saveCornerImage %d: %s\n", i, calibration->getLastError());
      return 1;
    }
  }
  LensModel lens;
  float innerErr = calibration->calibration(lens);
  char detail[128];
  snprintf(detail, sizeof(detail), "err=%.3fpx fx=%.1f(真值%.0f) center=(%.3f,%.3f)", innerErr,
           lens.focalLength.x * lens.imageSize.x, kFx, lens.focalCenter.x, lens.focalCenter.y);
  bool lensOk = innerErr >= 0 && innerErr < 0.5f &&
                fabs(lens.focalLength.x * lens.imageSize.x - kFx) < 15.0 &&
                fabs(lens.focalCenter.x - 0.5) < 0.02 && fabs(lens.focalCenter.y - 0.5) < 0.02;
  check(lensOk, "内参解算", detail);

  // ============ 2. 手眼标定 (Tsai + scale 列) ============
  printf("\n[2] 手眼标定 (bScale=true, 追踪器位移 x%.0f)\n", 1.0 / kGtScale);
  offset->setLensModel(lens, points.get());
  printf("  setLensModel done\n");
  for (int32_t i = 0; i < kPoseCount; i++) {
    // 追踪器位姿: 真实米制 → 追踪器单位 (平移缩放, 旋转不变), 转 avox
    int32_t n = offset->saveTrackCornerImage(toAvox(scaleTrackTranslation(poses[i].track2baseReal, kGtScale)),
                                             images[i].get());
    printf("  saveTrackCornerImage %d -> %d\n", i, n);
    if (n < 0) {
      printf("FAIL saveTrackCornerImage %d: %s\n", i, offset->getLastError());
      return 1;
    }
  }
  OffsetParamet paramet;
  paramet.startIndex = 3;
  paramet.computeRange = 1;
  paramet.bScale = true;
  CameraTrackOffset result;
  printf("  compute begin\n");
  int32_t used = offset->compute(paramet, result);
  printf("  compute done used=%d\n", used);
  // 诊断: 逐帧 PnP 直接反推的 camera2target (innerOffset) 与真值比
  for (int32_t i = 0; i < kPoseCount; i++) {
    FrameOffset trackOff, innerOff;
    offset->getFrameOffset(i, trackOff, innerOff);
    M4 got;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++) got.m[r][c] = (&innerOff.cameraPose.row0.x)[c * 4 + r];
    M4 gt = M4::rigidInverse(poses[i].target2camera);
    printf("  frame %d innerPnP rotDiff=%.3f°\n", i, rotDiffDeg(gt, got));
  }
  snprintf(detail, sizeof(detail), "used=%d offset=%.3fpx scale=%.4f(真值%.2f)", used,
           result.offset, result.cameraTrack.scale, kGtScale);
  check(used == kPoseCount && result.offset < 1.0f &&
            fabs(result.cameraTrack.scale - kGtScale) < 0.01,
        "手眼解算", detail);
  // camera2track 比对 (avox 转回 cv 约定比对)
  M4 gotC2T;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) gotC2T.m[i][j] = (&result.cameraTrack.camera2track.row0.x)[j * 4 + i];
  double rDiff = rotDiffDeg(gtCamera2track, gotC2T);
  double gtLen = sqrt(gtXyz[0] * gtXyz[0] + gtXyz[1] * gtXyz[1] + gtXyz[2] * gtXyz[2]);
  double gotT[3];
  gotC2T.getT(gotT);
  double tLen = sqrt(gotT[0] * gotT[0] + gotT[1] * gotT[1] + gotT[2] * gotT[2]);
  snprintf(detail, sizeof(detail), "rotDiff=%.3f° |t|=%.4f(真值%.4f)m", rDiff, tLen, gtLen);
  check(rDiff < 1.0 && fabs(tLen - gtLen) < 0.005, "camera2track 精度", detail);

  // ============ 3. PnP 反推 ============
  printf("\n[3] PnP 实时反推相机位姿\n");
  pnp->setLensModel(lens, points.get());
  Mat4x4d camPose;
  CalibOffset pnpOffset;
  bool bPnp = pnp->getCamPose(images[0].get(), camPose, &pnpOffset);
  // 真值: 相机在标定板系下位姿 = target2camera 的逆
  M4 gtCamPose = M4::rigidInverse(poses[0].target2camera);
  M4 gotPose;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) gotPose.m[i][j] = (&camPose.row0.x)[j * 4 + i];
  snprintf(detail, sizeof(detail), "rotDiff=%.3f° err=%.3fpx", rotDiffDeg(gtCamPose, gotPose),
           pnpOffset.avg);
  check(bPnp && rotDiffDeg(gtCamPose, gotPose) < 0.5 && pnpOffset.avg < 0.5, "PnP 反推", detail);

  // ============ 4. 序列一站式标定 ============
  printf("\n[4] 序列一站式标定 (IVideoCalibration)\n");
  video->setImagePoints(points.get());
  for (int32_t i = 0; i < kPoseCount; i++) {
    video->saveImage(images[i].get());
    video->saveTrackPose(toAvox(scaleTrackTranslation(poses[i].track2baseReal, kGtScale)));
  }
  VideoRTParamet rtPar;
  rtPar.minBlur = 0.0f;  // 棋盘格路径无 aruco SSIM 模糊度
  rtPar.minVisibility = 0.0f;
  VideoResult videoResult;
  bool bVideo = video->compute(rtPar, videoResult);
  snprintf(detail, sizeof(detail), "lensErr=%.3f offset=%.3fpx scale=%.4f", videoResult.lensOffset,
           videoResult.offsetResult.offset, videoResult.offsetResult.cameraTrack.scale);
  check(bVideo && videoResult.offsetResult.offset < 1.0f &&
            fabs(videoResult.offsetResult.cameraTrack.scale - kGtScale) < 0.01,
        "序列标定", detail);

  // 决定性诊断: 真值位姿投影 3D 点 vs 检测角点 (若重合则 PnP 错, 否则真值错)
  {
    PointCorners corners;
    video->getPointCorners(0, corners);
    double total = 0.0;
    M4 t2c = poses[0].target2camera;
    for (int32_t k = 0; k < corners.count; k++) {
      Vec3 p = {corners.points[k].x, corners.points[k].y, corners.points[k].z};
      double u, v;
      projectPoint(t2c, p, u, v);
      double du = u - corners.corners[k].x;
      double dv = v - corners.corners[k].y;
      total += sqrt(du * du + dv * dv);
    }
    printf("  GT位姿投影 vs 检测角点 平均偏差=%.4fpx (count=%d)\n", total / std::max(1, corners.count),
           corners.count);
    // 前 3 个与后 3 个角点的具体对照 (排查枚举方向翻转)
    for (int32_t k = 0; k < 3; k++) {
      Vec3 p = {corners.points[k].x, corners.points[k].y, corners.points[k].z};
      double u, v;
      projectPoint(t2c, p, u, v);
      printf("  corner[%d] uv=(%.1f,%.1f) gtProj=(%.1f,%.1f) 3d=(%.3f,%.3f,%.3f)\n", k,
             corners.corners[k].x, corners.corners[k].y, u, v, p.x, p.y, p.z);
    }
    for (int32_t k = corners.count - 3; k < corners.count; k++) {
      Vec3 p = {corners.points[k].x, corners.points[k].y, corners.points[k].z};
      double u, v;
      projectPoint(t2c, p, u, v);
      printf("  corner[%d] uv=(%.1f,%.1f) gtProj=(%.1f,%.1f)\n", k, corners.corners[k].x,
             corners.corners[k].y, u, v);
    }
  }

  // ============ 5. 运行时验证链路 ============
  printf("\n[5] 标定结果 → 运行时相机位姿换算\n");
  // 运行时: 摄像机在标定板(幕墙)系位姿 = base2target⁻¹ · track2base · camera2track
  // track2base 平移是追踪器单位, 须先乘标定出的 scale 转米制 (与第 0 帧 PnP 真值比对)
  M4 scaled0 = scaleTrackTranslation(poses[0].track2baseReal, kGtScale);
  for (int i = 0; i < 3; i++) scaled0.m[i][3] *= result.cameraTrack.scale;
  M4 b2t;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      b2t.m[i][j] = (&result.cameraTrack.base2target.row0.x)[j * 4 + i];
  M4 c2t;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      c2t.m[i][j] = (&result.cameraTrack.camera2track.row0.x)[j * 4 + i];
  // avox 矩阵是 cv 转置: avox 乘法 A.multiply(B) 对应 cv 的 B·A; 逐步用 cv 语义合成
  // cv 语义组合 (与 aoce/插件一致): camera2target = base2target · track2base · camera2track
  M4 camera2targetRT = M4::mul(M4::mul(b2t, scaled0), c2t);
  M4 target2cameraRT = M4::rigidInverse(camera2targetRT);
  M4 target2cameraGT = poses[0].target2camera;
  snprintf(detail, sizeof(detail), "rotDiff=%.3f°", rotDiffDeg(target2cameraGT, target2cameraRT));
  check(rotDiffDeg(target2cameraGT, target2cameraRT) < 1.0, "标定换算 vs PnP 真值", detail);

  printf("\n%s (失败 %d 项)\n", failCount == 0 ? "=== 全部通过 ===" : "=== 存在失败 ===", failCount);
  return failCount == 0 ? 0 : 1;
}
