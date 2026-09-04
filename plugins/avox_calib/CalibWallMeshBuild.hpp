#pragma once

// LED 幕墙 mesh → Aruco 标定数据 (移植自 aoce WallMesh/WallBuildItem/WallMeshBuild)
// 1 CalibWallMesh: 三角网格按 UV 栅格化成 LED 面板四边形阵列 (行列)
// 2 CalibWallItem: 每块生成 Aruco 贴图 (atlas) + 3D 角点 (UE4 cm → OpenCV m)
// 3 CalibWallMeshBuild: 多块管理, markerId 分配 (块内 列+行*列数, 跨块累加)

#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "CalibHelper.hpp"
#include "CalibImagePoints.hpp"

namespace avox {

// LED 面板四边形 (顶点序: 左上,右上,左下,右下, UV 排序后)
struct CalibWallPanel {
  vec3f vertices[4] = {};
  vec2f uvs[4] = {};
  int32_t col = 0;
  int32_t row = 0;
};

// 幕墙网格: 三角面 → 面板四边形栅格
class CalibWallMesh {
 public:
  std::vector<CalibWallPanel> walls;
  int32_t colNum = 1;
  float wallWidth = 0.5f;
  float wallHeight = 0.5f;

 public:
  // mesh 位置单位 cm (FBX 原生)
  bool loadMesh(ISceneMesh& mesh);
};

// 单块幕墙的 Aruco 数据
struct CalibWallItem {
  CalibWallMesh wallMesh;
  int32_t startId = 0;
  int32_t colNum = 1;
  int32_t rowNum = 1;
  int32_t widthPixel = 216;
  int32_t heightPixel = 216;
  // 3D 角点 (OpenCV 系, 米): 每格 4 点 TL,TR,BR,BL, arucoId 契约对齐
  std::vector<cv::Point3f> arucoPoints;
  // Aruco 贴图 (灰度 atlas, 播放到 LED 屏用)
  cv::Mat atlas;

 public:
  bool loadMesh(ISceneMesh* mesh, int32_t widthPixel_, int32_t heightPixel_);
  bool generateData(int32_t startId_, int32_t scale, CalibArucoType arucoType, bool bCreateMark);
};

class CalibWallMeshBuild : public ILedMeshBuild {
 private:
  std::vector<std::unique_ptr<CalibWallItem>> wallMeshs;
  CalibArucoType arucoType = CalibArucoType::dict6x6_1000;
  int32_t startId = 0;
  bool bCreateMark = true;
  std::string lastError;

 public:
  CalibWallMeshBuild() = default;
  virtual ~CalibWallMeshBuild() = default;

 public:
  virtual bool loadMesh(ISceneMesh* mesh, int32_t widthPixel = 216,
                        int32_t heightPixel = 216) override;
  virtual void updateWall(CalibArucoType arucoType, int32_t maxArucoIndex = 0) override;
  virtual bool createArucoPoints(IImagePoints* imagePoints) override;
  virtual bool saveArucoImage(int32_t itemIndex, const char* pngPath) override;
  virtual float getDistance(const Mat4x4d& camPose) override;
  virtual int32_t getItemCount() override;
  virtual void clear() override;
  virtual const char* getLastError() override;
};

}  // namespace avox
