#pragma once

// ============ 虚拟制片相机标定 (移植自 aoce 虚拟制片标定方案) ============
// 流程: N 个位置同时拍标定图案(Aruco 幕墙/棋盘格) + 记录追踪器位姿 →
//       内参(LensModel) → 每帧 PnP → 手眼(AI=XB, Tsai+scale 列) + SVD 求 base2target
//       → 重投影像素误差验证。运行时 trackPose × cameraTrack 即虚拟相机位姿。
// 实现: plugins/avox_calib (OpenCV); 业务经 AvoxManager 各 calib hub 查表拿实例。
// 算法说明与精度坑: doc/plan/虚拟制片标定移植方案.md
// 跨 DLL 安全: 接口只传 IImageBuffer*/POD/原始指针, 不传 STL (范本 AvoxVision.h)。

#include "AvoxDef.h"
#include "AvoxImage.h"
#include "AvoxMath.h"
#include "AvoxScene.h"

namespace avox {

// 角点图案类型
enum class CalibCornerType : int32_t {
  other = 0,
  chessboard,  // 棋盘格 (张正友)
  aruco,       // Aruco 标识 (LED 幕墙)
  user,        // 用户自注入 3D 点
};

// 对应 OpenCV cv::aruco::PredefinedDictionaryType
enum class CalibArucoType : int32_t {
  dict4x4_50 = 0,
  dict4x4_100,
  dict4x4_250,
  dict4x4_1000,
  dict5x5_50,
  dict5x5_100,
  dict5x5_250,
  dict5x5_1000,
  dict6x6_50,
  dict6x6_100,
  dict6x6_250,
  dict6x6_1000,
  dict7x7_50,
  dict7x7_100,
  dict7x7_250,
  dict7x7_1000,
  dictArucoOriginal,
  dictApriltag16h5,
  dictApriltag25h9,
  dictApriltag36h10,
  dictApriltag36h11,
};

// 镜头内参 + 畸变 (内参按图像尺寸归一化存储, 焦距/中心乘 imageSize 即像素内参;
// 非平面标定(如弧形 LED 幕墙)时 focalLength 传图像宽高比即可解出正确 fx/fy 比例)
struct LensModel {
  vec2d imageSize = vec2d(0.0, 0.0);
  // 焦距(XY)比例, 乘传感器尺寸 = 物理焦段
  vec2d focalLength = vec2d(0.0, 0.0);
  // 焦点中心, 默认 0.5,0.5
  vec2d focalCenter = vec2d(0.5, 0.5);
  // 畸变系数
  double k1 = 0.0;
  double k2 = 0.0;
  double p1 = 0.0;
  double p2 = 0.0;
  double k3 = 0.0;
};

// 标定产物: 追踪器与摄像机/标定板的空间关系三要素
struct CameraTrack {
  // 追踪器坐标系到标定板坐标系的变换 (SVD 最小二乘解)
  Mat4x4d base2target;
  // 摄像机相对追踪器的姿态 (手眼矩阵 X)
  Mat4x4d camera2track;
  // 追踪器位移缩放 (Redspy 等追踪器位移与真实尺寸存在未知比例)
  double scale = 1.0;
};

// 误差对: avg=平均重投影像素误差, max=最大误差
struct CalibOffset {
  float avg = 0.0f;
  float max = 0.0f;
};

// 某帧相机姿态与重投影误差
struct FrameOffset {
  Mat4x4d cameraPose;
  CalibOffset offset;
};

// 手眼标定结果 + 验证误差 + 建议保留帧位掩码 (bit i = 第 i 帧)
struct CameraTrackOffset {
  CameraTrack cameraTrack;
  // 全部帧平均重投影像素误差
  float offset = 0.0f;
  // 参与最优解的帧索引位掩码
  uint64_t suggest = 0;
};

// 棋盘格规格 (width/height 为内角点数, size 为格子边长, 单位与 3D 点一致建议米)
struct ChessboardInfo {
  int32_t width = 0;
  int32_t height = 0;
  float size = 0.0f;
};

// Aruco 识别情况 (用于视频序列筛选可用帧)
struct CornerMass {
  // 识别到的标识数
  int32_t visible = 0;
  // 被拒绝的候选数
  int32_t rejected = 0;
  // 模糊度 (0-1, 越高越清晰, 基于 SSIM)
  float blur = 0.0f;
  // 标识占画面面积比 (0-1)
  float visibility = 0.0f;
};

// 手眼计算参数
struct OffsetParamet {
  // 参与计算的帧位掩码, 0 为全选
  uint64_t computeIndexs = 0;
  // 最少组合大小 (每组 C(N,k) 排列组合取最优, 默认 3)
  int32_t startIndex = 3;
  // 组合大小增量 (1 即加算 4 组合, 计算量增加)
  int32_t computeRange = 0;
  // 追踪器位移是否有缩放 (true 用带 scale 列的 Tsai 改进求解)
  bool bScale = false;
};

// 视频序列标定参数
struct VideoRTParamet {
  // 最小模糊度阈值
  float minBlur = 0.4f;
  // 最小可见占比阈值
  float minVisibility = 0.1f;
  // 更大组合计算范围
  int32_t bRange = 0;
};

// 视频序列标定结果
struct VideoResult {
  // 内参标定重投影误差
  float lensOffset = 0.0f;
  LensModel lensModel;
  CameraTrackOffset offsetResult;
};

// 二维识别角点与三维顶点 (引用内部数据, 仅当次调用后立即使用)
struct PointCorners {
  vec2d imageSize = vec2d(0.0, 0.0);
  int32_t count = 0;
  // 像素 UV (count 个)
  vec2f* corners = nullptr;
  // 对应三维顶点 (count 个, 与 corners 一一对应)
  vec3f* points = nullptr;
};

// ============ 接口 ============

// 角点三维顶点容器 + 图案识别 (棋盘格/Aruco/用户注入)。
// 3D 点来源: 1) setChessboardInfo 自动生成 2) setPoints 注入 (如 LED 幕墙 Mesh 顶点)。
// Aruco 契约: markerId m 的第 c 个角点对应 3D 点索引 (m - startArucoId) * 4 + c,
// 注入点集必须按此排列。识别/检测细节由标定类内部使用。
class IImagePoints {
 public:
  virtual ~IImagePoints() = default;

 public:
  // 棋盘格模式: 自动生成平面 3D 角点 (行优先, 左上为原点)
  virtual void setChessboardInfo(const ChessboardInfo& info) = 0;
  // Aruco 模式: 字典 + 起始 markerId (3D 点须经 setPoints 按 Aruco 契约注入)
  virtual void setArucoInfo(CalibArucoType arucoType, int32_t startArucoId) = 0;
  // 注入自定义 3D 点 (米制建议, 与识别图案一一对应)
  virtual void setPoints(int32_t count, const vec3f* points) = 0;
  virtual int32_t getPointCount() = 0;
  // 通用坐标 3D 点 (OpenCV 系: x右 y下 z前)
  virtual vec3f getPoint(int32_t index) = 0;
  // 最近一次 Aruco 识别情况
  virtual CornerMass getCornerMass() = 0;
  virtual CalibCornerType getCornerType() = 0;
};

// 镜头内参/畸变标定 (张正友): N 张不同姿态图案 → calibrateCamera
class ICameraCalibration {
 public:
  virtual ~ICameraCalibration() = default;

 public:
  // 图像尺寸 + 3D 点映射对象 (图像尺寸取首次 saveCornerImage 的输入)
  virtual void setImagePoints(const vec2d& imageSize, IImagePoints* imagePoints) = 0;
  // 清除已保存的标定帧
  virtual void reset() = 0;
  // 保存一张图案图像并识别角点, 返回当前帧数, -1 为无效 (未识别/尺寸不符)
  virtual int32_t saveCornerImage(IImageBuffer* image) = 0;
  // 删除某帧, 返回剩余帧数
  virtual int32_t removeFrame(int32_t frameIndex) = 0;
  // 解算内参; guess=true 时以传入 lensModel 为初值 (CALIB_USE_INTRINSIC_GUESS)。
  // 返回整体重投影误差 (像素), 结果写入 lensModel; 失败返回负值
  virtual float calibration(LensModel& lensModel, bool guess = false) = 0;
  // 某帧解算后的相机姿态 (标定板系下) 与该帧误差
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& frameOffset) = 0;
  virtual const char* getLastError() = 0;
};

// 手眼标定 (眼在手上): 固定标定板移动相机+追踪器, N≥3 组 (图案图+trackPose)
// → AX=XB 求摄像机相对追踪器姿态。追踪器姿态请先转换到 OpenCV 坐标系
// (convertUE4ToOpenCV), 算法内部约定与 OpenCV 一致。
class ICameraOffset {
 public:
  virtual ~ICameraOffset() = default;

 public:
  // 内参 (先由 ICameraCalibration 解出) + 3D 点映射对象
  virtual void setLensModel(const LensModel& lensModel, IImagePoints* imagePoints) = 0;
  virtual void reset() = 0;
  // 记录一组数据: 追踪器位姿 (OpenCV 系) + 图案图像, 返回当前帧数, -1 无效
  virtual int32_t saveTrackCornerImage(const Mat4x4d& trackPose, IImageBuffer* image) = 0;
  virtual int32_t removeFrame(int32_t frameIndex) = 0;
  // 排列组合所有数据选重投影误差最优解; 返回参与计算的帧数
  virtual int32_t compute(const OffsetParamet& paramet, CameraTrackOffset& cameraOffset) = 0;
  // 某帧: trackOffset=经标定换算的相机姿态与误差, innerOffset=PnP 直接反推的姿态与误差
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& trackOffset,
                              FrameOffset& innerOffset) = 0;
  // 某帧识别的角点 2D/3D 数据 (指针引用内部数据, 供图优化 fillData)
  virtual bool getPointCorners(int32_t frameIndex, PointCorners& pointCorners) = 0;
  // 外部更新手眼结果 (如 g2o 再优化后), 重算全部帧 Track 误差, 返回平均误差
  virtual float updateCameraTrack(const CameraTrack& cameraTrack) = 0;
  virtual const char* getLastError() = 0;
};

// PnP 实时反推相机姿态 (运行时验证/免标定板追踪): 已知内参 + 幕墙 3D 点,
// 每帧画面即可反推相机在标定板(幕墙)坐标系下姿态
class IPnpCameraPose {
 public:
  virtual ~IPnpCameraPose() = default;

 public:
  virtual void setLensModel(const LensModel& lensModel, IImagePoints* imagePoints) = 0;
  // 反推相机姿态 (角点坐标系下), offset 可选返回重投影像素误差 (应在 1 像素左右)
  virtual bool getCamPose(IImageBuffer* image, Mat4x4d& cameraPose,
                          CalibOffset* offset = nullptr) = 0;
  // 已知相机姿态时, 计算该画面与姿态的重投影偏差 (看位置准确度, 5 像素内可接受)
  virtual bool getCamOffset(IImageBuffer* image, const Mat4x4d& cameraPose,
                            CalibOffset& offset) = 0;
  virtual CornerMass getCornerMass() = 0;
  virtual const char* getLastError() = 0;
};

// LED 幕墙 mesh → Aruco 标定 (移植自 aoce WallMeshBuild):
// 加载幕墙网格 (ISceneImport, AvoxScene.h) → 按 UV 栅格化面板 →
// 分配各块 markerId → 生成 3D 角点 (IImagePoints, arucoId 契约对齐) + 标定贴图。
// 贴图播放到 LED 屏上拍摄即可标定; markerId = startId + 块内列 + 行*列数。
class ILedMeshBuild {
 public:
  virtual ~ILedMeshBuild() = default;

 public:
  // 加载一块幕墙 mesh (widthPixel/heightPixel = 每格贴图分辨率, 兼作格宽高比检查)
  virtual bool loadMesh(ISceneMesh* mesh, int32_t widthPixel = 216,
                        int32_t heightPixel = 216) = 0;
  // 为各块分配 Aruco id (maxArucoIndex=0 时用字典上限; 超限自动缩格)
  virtual void updateWall(CalibArucoType arucoType, int32_t maxArucoIndex = 0) = 0;
  // 生成全部块的 3D 角点并注入 IImagePoints (需先 setArucoInfo 或由此内部设置)
  virtual bool createArucoPoints(IImagePoints* imagePoints) = 0;
  // 导出某块 Aruco 贴图 png (投到 LED 屏上拍摄)
  virtual bool saveArucoImage(int32_t itemIndex, const char* pngPath) = 0;
  // 相机位姿(标定板系)到幕墙最近格的距离 (米, 运行时调试用)
  virtual float getDistance(const Mat4x4d& camPose) = 0;
  virtual int32_t getItemCount() = 0;
  virtual void clear() = 0;
  virtual const char* getLastError() = 0;
};

// 某帧识别的角点数据 (2D/3D), 供图优化器 fillData
struct TrackCorners {
  // 当前摄像机在标定板系下姿态 (PnP 结果取逆; identity/invalid 则由优化器按
  // base2target·track2base·scale·camera2track 反推初值)
  Mat4x4d cameraPose;
  // 原始追踪器姿态 (追踪器单位, OpenCV 系)
  Mat4x4d trackPose;
  PointCorners pointCorners;
};

// 手眼图优化参数
struct HandEyeParamet {
  // 固定 scale 不参与优化
  bool bFixScale = false;
  // 手眼位姿边 Huber 核 delta
  float handEyeDelta = 0.01f;
  // 手眼边鲁棒核 (初值较差时开启更稳)
  bool robustHandEye = true;
  // 重投影边鲁棒核 (Huber delta=1.0)
  bool projectionHand = true;
};

// 手眼标定图优化器 (g2o 实现, 注册名 "g2o"): 参考st_handeye_graph,
// 同时优化 camera2track/base2target/每帧target2camera/scale,
// 对初值不敏感且天然吸收追踪器位移缩放
class ICameraTrackOptimizer {
 public:
  virtual ~ICameraTrackOptimizer() = default;

 public:
  virtual void fillData(int32_t count, const TrackCorners* dataPtr) = 0;
  virtual CameraTrack compute(const HandEyeParamet& paramet, const LensModel& lensModel,
                              const CameraTrack& cameraTrack) = 0;
  virtual const char* getLastError() = 0;
};

// 内参+畸变+每帧位姿联合 BA 优化器 (g2o 实现, 注册名 "g2o"):
// 弧形(非平面)标定物上 calibrateCamera 初值估计失效会发散, 此路径可解
class ICalibrationOptimizer {
 public:
  virtual ~ICalibrationOptimizer() = default;

 public:
  virtual void fillData(int32_t count, const TrackCorners* dataPtr) = 0;
  // 以传入内参为初值联合优化, 返回优化后内参; 每帧位姿经 getCameraPose 取回
  virtual LensModel compute(const LensModel& lensModel) = 0;
  virtual bool getCameraPose(int32_t frameIndex, Mat4x4d& cameraPose) = 0;
  // 某帧在优化后内参+位姿下的重投影误差 (像素)
  virtual float getOffset(int32_t frameIndex) = 0;
  virtual const char* getLastError() = 0;
};

// 视频序列一站式标定: 逐帧喂图像 (+trackPose) → 自动分 clip/解内参/PnP/手眼/误差
class IVideoCalibration {
 public:
  virtual ~IVideoCalibration() = default;

 public:
  virtual void setImagePoints(IImagePoints* imagePoints) = 0;
  // 保存一帧视频画面并识别 (尺寸不一致自动缩放到首帧尺寸), 返回当前帧数
  virtual int32_t saveImage(IImageBuffer* image) = 0;
  // 保存与当前帧对应的追踪器位姿 (按帧序一一对应)
  virtual void saveTrackPose(const Mat4x4d& trackPose) = 0;
  virtual int32_t removeFrame(int32_t frameIndex) = 0;
  // 无延迟对齐的标定: 无 track 数据只解内参 (videoResult.lensOffset);
  // 有 track 数据连带手眼 (bScale 自动开)
  virtual bool compute(const VideoRTParamet& videoPar, VideoResult& videoResult) = 0;
  // 某帧: innerOffset=PnP 直接反推, trackOffset=经标定换算
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& innerOffset,
                              FrameOffset* trackOffset = nullptr) = 0;
  // 某帧识别的角点 2D/3D 数据 (指针引用内部数据)
  virtual bool getPointCorners(int32_t frameIndex, PointCorners& pointCorners) = 0;
  // 外部更新手眼结果 (如 g2o 优化后) 并重算全部帧误差, 返回平均误差
  virtual float updateCameraTrack(const CameraTrack& cameraTrack) = 0;
  // 序列化角点分析结果 (识别耗时长, 缓存后离线重算)
  virtual bool saveBinary(const char* fileName) = 0;
  virtual bool loadBinary(const char* fileName) = 0;
  virtual const char* getLastError() = 0;
};

// ============ 内参辅助 (纯数学, header-only) ============

// 像素内参 fx
inline double getLensFx(const LensModel& lensModel) {
  return lensModel.focalLength.x * lensModel.imageSize.x;
}
// 垂直 FOV (弧度), 依据 fy
inline double getLensFovY(const LensModel& lensModel) {
  return 2.0 * atan(lensModel.imageSize.y * 0.5 / (lensModel.focalLength.y * lensModel.imageSize.y));
}
// 水平 FOV (弧度), 依据 fx (UE CineCamera 使用水平 FOV)
inline double getLensFovX(const LensModel& lensModel) {
  return 2.0 * atan(lensModel.imageSize.x * 0.5 / (lensModel.focalLength.x * lensModel.imageSize.x));
}
// 由垂直 FOV (弧度) 与图像尺寸生成理想无畸变 LensModel
inline LensModel getLensModelFromFov(double fovY, int32_t width, int32_t height) {
  LensModel model;
  model.imageSize = vec2d((double)width, (double)height);
  double fy = height * 0.5 / tan(fovY * 0.5);
  model.focalLength = vec2d(fy / width, fy / height);
  return model;
}

}  // namespace avox
