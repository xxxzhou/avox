#include "CalibWallMeshBuild.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

inline vec3f vecSub(const vec3f& a, const vec3f& b) {
  return vec3f(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline bool nearlyEqual(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

inline float vecLen(const vec3f& a, const vec3f& b) {
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 面板角点按 uv.x + uv.y*100 升序 → 行优先排列 (左上,右上,左下,右下)
void sortUV(CalibWallPanel& wall) {
  int32_t indexs[4] = {0, 1, 2, 3};
  std::sort(indexs, indexs + 4, [&](int32_t a, int32_t b) {
    return wall.uvs[a].x + wall.uvs[a].y * 100 < wall.uvs[b].x + wall.uvs[b].y * 100;
  });
  vec3f tempVec[4];
  vec2f tempUV[4];
  for (int32_t i = 0; i < 4; i++) {
    tempVec[i] = wall.vertices[i];
    tempUV[i] = wall.uvs[i];
  }
  for (int32_t i = 0; i < 4; i++) {
    wall.vertices[i] = tempVec[indexs[i]];
    wall.uvs[i] = tempUV[indexs[i]];
  }
}

bool similarWall(const CalibWallPanel& left, const CalibWallPanel& right) {
  for (int32_t i = 0; i < 4; i++) {
    if (!nearlyEqual(left.uvs[i].x, right.uvs[i].x) ||
        !nearlyEqual(left.uvs[i].y, right.uvs[i].y)) {
      return false;
    }
  }
  return true;
}

// CalibArucoType → 字典 id 总数
int32_t getArucoCount(CalibArucoType type) {
  switch (type) {
    case CalibArucoType::dict4x4_50: return 50;
    case CalibArucoType::dict4x4_100: return 100;
    case CalibArucoType::dict4x4_250: return 250;
    case CalibArucoType::dict4x4_1000: return 1000;
    case CalibArucoType::dict5x5_50: return 50;
    case CalibArucoType::dict5x5_100: return 100;
    case CalibArucoType::dict5x5_250: return 250;
    case CalibArucoType::dict5x5_1000: return 1000;
    case CalibArucoType::dict6x6_50: return 50;
    case CalibArucoType::dict6x6_100: return 100;
    case CalibArucoType::dict6x6_250: return 250;
    case CalibArucoType::dict6x6_1000: return 1000;
    case CalibArucoType::dict7x7_50: return 50;
    case CalibArucoType::dict7x7_100: return 100;
    case CalibArucoType::dict7x7_250: return 250;
    case CalibArucoType::dict7x7_1000: return 1000;
    case CalibArucoType::dictArucoOriginal: return 1024;
    case CalibArucoType::dictApriltag16h5: return 30;
    case CalibArucoType::dictApriltag25h9: return 35;
    case CalibArucoType::dictApriltag36h10: return 2320;
    case CalibArucoType::dictApriltag36h11: return 587;
    default: return 1000;
  }
}

}  // namespace

// ============ CalibWallMesh ============

bool CalibWallMesh::loadMesh(ISceneMesh& mesh) {
  walls.clear();
  colNum = 1;
  int32_t triCount = mesh.getTriangleCount();
  const vec3i* triData = mesh.getTriangles();
  const vec3f* vecData = mesh.getPositions();
  const vec2f* uvData = mesh.getUVs();
  if (!triData || !vecData || !uvData || triCount <= 0) {
    return false;
  }
  // 三角形 → 面板四边形: 三个点推理第四个点, 一面板二三角去重
  for (int32_t i = 0; i < triCount; i++) {
    CalibWallPanel panel;
    panel.vertices[0] = vecData[triData[i].x];
    panel.vertices[1] = vecData[triData[i].y];
    panel.vertices[2] = vecData[triData[i].z];
    panel.uvs[0] = uvData[triData[i].x];
    panel.uvs[1] = uvData[triData[i].y];
    panel.uvs[2] = uvData[triData[i].z];
    // UV 距离最长的边对角, 以对角顶点为原点推理第四点
    float d01 = std::sqrt(std::pow(panel.uvs[0].x - panel.uvs[1].x, 2) +
                          std::pow(panel.uvs[0].y - panel.uvs[1].y, 2));
    float d02 = std::sqrt(std::pow(panel.uvs[0].x - panel.uvs[2].x, 2) +
                          std::pow(panel.uvs[0].y - panel.uvs[2].y, 2));
    float d12 = std::sqrt(std::pow(panel.uvs[1].x - panel.uvs[2].x, 2) +
                          std::pow(panel.uvs[1].y - panel.uvs[2].y, 2));
    float maxDist = std::max(d01, std::max(d02, d12));
    int32_t originIndex = 0;
    int32_t startIndex = 1;
    int32_t endIndex = 2;
    if (nearlyEqual(d01, maxDist)) {
      originIndex = 2;
      startIndex = 0;
      endIndex = 1;
    } else if (nearlyEqual(d02, maxDist)) {
      originIndex = 1;
      startIndex = 0;
      endIndex = 2;
    }
    vec2f originUV = panel.uvs[originIndex];
    vec3f origin3d = panel.vertices[originIndex];
    panel.uvs[3] = vec2f(originUV.x + (panel.uvs[startIndex].x - originUV.x) +
                             (panel.uvs[endIndex].x - originUV.x),
                         originUV.y + (panel.uvs[startIndex].y - originUV.y) +
                             (panel.uvs[endIndex].y - originUV.y));
    panel.vertices[3] = vec3f(origin3d.x + (panel.vertices[startIndex].x - origin3d.x) +
                                  (panel.vertices[endIndex].x - origin3d.x),
                              origin3d.y + (panel.vertices[startIndex].y - origin3d.y) +
                                  (panel.vertices[endIndex].y - origin3d.y),
                              origin3d.z + (panel.vertices[startIndex].z - origin3d.z) +
                                  (panel.vertices[endIndex].z - origin3d.z));
    sortUV(panel);
    bool newWall = true;
    for (CalibWallPanel& wall : walls) {
      if (similarWall(panel, wall)) {
        newWall = false;
        break;
      }
    }
    if (newWall) {
      walls.push_back(panel);
    }
  }
  // 面板数检查 (一面板二三角)
  int32_t panelNum = triCount / 2;
  if (walls.empty() || (int32_t)walls.size() != panelNum) {
    LOGFLF(LogLevel::warn, "wall mesh panels: ", (int32_t)walls.size(),
           " not equal half triangles: ", triCount);
    return false;
  }
  std::sort(walls.begin(), walls.end(), [&](const CalibWallPanel& a, const CalibWallPanel& b) {
    return a.uvs[0].x + a.uvs[0].y * 100 < b.uvs[0].x + b.uvs[0].y * 100;
  });
  // 确定列数: 首行 uv0.x 严格递增的个数
  for (int32_t i = 1; i < panelNum; i++) {
    float prevU = walls[i - 1].uvs[0].x;
    float curU = walls[i].uvs[0].x;
    if (curU < prevU || nearlyEqual(curU, prevU)) {
      break;
    }
    colNum++;
  }
  wallWidth = vecLen(walls[0].vertices[0], walls[0].vertices[1]);
  wallHeight = vecLen(walls[0].vertices[0], walls[0].vertices[2]);
  int32_t row = 0;
  int32_t col = 0;
  for (CalibWallPanel& wall : walls) {
    wall.col = col;
    wall.row = row;
    float widthOffset = std::fabs(vecLen(wall.vertices[0], wall.vertices[1]) - wallWidth);
    float heightOffset = std::fabs(vecLen(wall.vertices[0], wall.vertices[2]) - wallHeight);
    if (widthOffset > 0.01f || heightOffset > 0.01f) {
      LOGFLF(LogLevel::warn, "wall col:", col, " row:", row, " size offset:", widthOffset, ",",
             heightOffset);
    }
    if (++col == colNum) {
      col = 0;
      row++;
    }
  }
  return true;
}

// ============ CalibWallItem ============

bool CalibWallItem::loadMesh(ISceneMesh* mesh, int32_t widthPixel_, int32_t heightPixel_) {
  widthPixel = widthPixel_;
  heightPixel = heightPixel_;
  if (!wallMesh.loadMesh(*mesh)) {
    LOGFLF(LogLevel::warn, "WallItem load mesh failed");
    return false;
  }
  colNum = wallMesh.colNum;
  int32_t wallCount = (int32_t)wallMesh.walls.size();
  rowNum = wallCount / colNum;
  if (wallCount % colNum != 0) {
    rowNum = rowNum + 1;
    LOGFLF(LogLevel::warn, "wall mesh not rectangle: ", wallCount, " / ", colNum);
  }
  return true;
}

bool CalibWallItem::generateData(int32_t startId_, int32_t scale, CalibArucoType arucoType,
                                 bool bCreateMark_) {
  if (rowNum <= 0 || colNum <= 0) {
    return false;
  }
  auto dict = cv::aruco::getPredefinedDictionary(
      getCvArucoType(CalibArucoType((int32_t)arucoType)));
  startId = startId_;
  int32_t pixel = std::min(widthPixel * scale, heightPixel * scale);
  int32_t atlasW = colNum * widthPixel * scale;
  int32_t atlasH = rowNum * heightPixel * scale;
  cv::Mat cornerdMat(cv::Size(atlasW, atlasH), CV_8UC1, cv::Scalar(255));
  // mark 本身分段 + 周边白框段
  int32_t totalSize = dict.markerSize + 2;
  int32_t boardPixel = pixel / totalSize;
  int32_t arucoPixel = pixel - 2 * boardPixel;
  // aruco 四角 UV (左上,右上,右下,左下)
  vec2f arucoUV[4] = {{(float)boardPixel / pixel, (float)boardPixel / pixel},
                      {(float)(boardPixel + arucoPixel) / pixel, (float)boardPixel / pixel},
                      {(float)(boardPixel + arucoPixel) / pixel,
                       (float)(boardPixel + arucoPixel) / pixel},
                      {(float)boardPixel / pixel, (float)(boardPixel + arucoPixel) / pixel}};
  int32_t cellCol = colNum / scale;
  for (int32_t j = 0; j < rowNum / scale; j++) {
    for (int32_t i = 0; i < cellCol; i++) {
      cv::Mat panelMat(pixel, pixel, CV_8UC1, cv::Scalar(255));
      cv::Point2i startPoint(i * widthPixel * scale, j * heightPixel * scale);
      if (bCreateMark_ && i == 0 && j == 0) {
        // 每块第一格写块序号
        std::string msg = std::to_string(startId);
        double fontScale = 1.8;
        int thickness = 3;
        int baseline = 0;
        cv::Size textSize =
            cv::getTextSize(msg, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);
        cv::Point textOrg((pixel - textSize.width) / 2, (pixel + textSize.height) / 2);
        cv::putText(panelMat, msg, textOrg, cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(0),
                    cv::LINE_8);
        cv::line(panelMat, cv::Point(0, 0), cv::Point(0, pixel), cv::Scalar(0), 10);
        cv::line(panelMat, cv::Point(0, 0), cv::Point(pixel, 0), cv::Scalar(0), 10);
      } else {
        int32_t arucoIndex = startId + i + j * cellCol;
        cv::Mat arucoMat;
        dict.generateImageMarker(arucoIndex, arucoPixel, arucoMat);
        arucoMat.copyTo(
            panelMat(cv::Rect(boardPixel, boardPixel, arucoPixel, arucoPixel)));
      }
      panelMat.copyTo(cornerdMat(cv::Rect(startPoint, panelMat.size())));
      // 面板四角双线性插值出 aruco 四角 3D 位置
      int32_t leftTop = j * scale * colNum + i * scale;
      int32_t leftBottom = leftTop + (scale - 1) * colNum;
      int32_t rightTop = leftTop + scale - 1;
      vec3f ltVec = wallMesh.walls[leftTop].vertices[0];
      vec3f vecX = vecSub(wallMesh.walls[rightTop].vertices[1], ltVec);
      vec3f vecY = vecSub(wallMesh.walls[leftBottom].vertices[2], ltVec);
      for (int32_t c = 0; c < 4; c++) {
        vec3f pos = vec3f(ltVec.x + vecX.x * arucoUV[c].x + vecY.x * arucoUV[c].y,
                          ltVec.y + vecX.y * arucoUV[c].x + vecY.y * arucoUV[c].y,
                          ltVec.z + vecX.z * arucoUV[c].x + vecY.z * arucoUV[c].y);
        // UE4(X后,Y右,Z上, cm) → OpenCV(X右,Y下,Z前, m): (x,y,z)→(y,-z,x)*0.01
        cv::Point3f cvPos = {pos.y * 0.01f, -pos.z * 0.01f, pos.x * 0.01f};
        arucoPoints.push_back(cvPos);
      }
    }
  }
  atlas = cornerdMat.clone();
  return true;
}

// ============ CalibWallMeshBuild ============

bool CalibWallMeshBuild::loadMesh(ISceneMesh* mesh, int32_t widthPixel, int32_t heightPixel) {
  AVOX_CALIB_TRY;
  auto item = std::make_unique<CalibWallItem>();
  if (!item->loadMesh(mesh, widthPixel, heightPixel)) {
    lastError = "load wall mesh failed";
    return false;
  }
  wallMeshs.push_back(std::move(item));
  return true;
  AVOX_CALIB_CATCH_RET(false)
}

void CalibWallMeshBuild::updateWall(CalibArucoType arucoType_, int32_t maxArucoIndex) {
  arucoType = arucoType_;
  int32_t cid = startId;
  int32_t count = 0;
  int32_t maxCount = getArucoCount(arucoType);
  if (maxArucoIndex > 0) {
    maxCount = std::min(maxArucoIndex, maxCount);
  }
  for (auto& item : wallMeshs) {
    count += (int32_t)item->wallMesh.walls.size();
  }
  // 总格数超字典上限时按 sqrt 缩格 (多格合一块)
  int32_t cscale = (count + maxCount - 1) / maxCount;
  float fscale = std::sqrt(float(cscale));
  if (fscale != std::floor(fscale)) {
    fscale += 1;
  }
  int32_t scale = (int32_t)std::floor(fscale);
  for (auto& item : wallMeshs) {
    item->generateData(cid, scale, arucoType, bCreateMark);
    int32_t used = (int32_t)item->wallMesh.walls.size() / (scale * scale);
    cid += used;
  }
}

bool CalibWallMeshBuild::createArucoPoints(IImagePoints* imagePoints) {
  AVOX_CALIB_TRY;
  CalibImagePoints* points = dynamic_cast<CalibImagePoints*>(imagePoints);
  if (!points) {
    lastError = "imagePoints must create from imagePointsHub";
    return false;
  }
  points->setArucoInfo(arucoType, startId);
  std::vector<vec3f> allPoints;
  for (auto& item : wallMeshs) {
    // 诊断: 每块 3D 点包围盒 (确认单位/世界变换正确)
    if (!item->arucoPoints.empty()) {
      cv::Point3f mn = item->arucoPoints[0], mx = item->arucoPoints[0];
      for (const cv::Point3f& p : item->arucoPoints) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
      }
      LOGFLF(LogLevel::info, "wall item bbox min(", mn.x, ",", mn.y, ",", mn.z, ") max(", mx.x,
             ",", mx.y, ",", mx.z, ") points:", (int32_t)item->arucoPoints.size(),
             " grid:", item->colNum, "x", item->rowNum);
    }
    for (const cv::Point3f& p : item->arucoPoints) {
      allPoints.push_back(vec3f(p.x, p.y, p.z));
    }
  }
  points->setPoints((int32_t)allPoints.size(), allPoints.data());
  LOGFLF(LogLevel::info, "wall mesh aruco points: ", (int32_t)allPoints.size());
  return true;
  AVOX_CALIB_CATCH_RET(false)
}

bool CalibWallMeshBuild::saveArucoImage(int32_t itemIndex, const char* pngPath) {
  if (itemIndex < 0 || itemIndex >= (int32_t)wallMeshs.size()) {
    return false;
  }
  const cv::Mat& atlas = wallMeshs[itemIndex]->atlas;
  if (atlas.empty()) {
    return false;
  }
  return cv::imwrite(pngPath, atlas);
}

float CalibWallMeshBuild::getDistance(const Mat4x4d& camPose) {
  // 相机位置/朝向 (OpenCV 系) 与各面板第一顶点的最大投影角 → 最近距离
  float maxAngle = -1.0f;
  float minDistance = 0.0f;
  auto ledAngle = [&](const vec3f& pos, float& distance) {
    vec3f origin = vec3f((float)camPose.row3.x, (float)camPose.row3.y, (float)camPose.row3.z);
    vec3f forward = vec3f((float)camPose.row2.x, (float)camPose.row2.y, (float)camPose.row2.z);
    float dx = pos.x - origin.x, dy = pos.y - origin.y, dz = pos.z - origin.z;
    distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance <= 0.0f) return 0.0f;
    return (forward.x * dx + forward.y * dy + forward.z * dz) / distance;
  };
  for (auto& item : wallMeshs) {
    if (item->wallMesh.walls.empty()) continue;
    float distance = 0.0f;
    float angle = ledAngle(item->wallMesh.walls[0].vertices[0], distance);
    if (angle > maxAngle) {
      maxAngle = angle;
      minDistance = distance;
    }
  }
  return minDistance;
}

int32_t CalibWallMeshBuild::getItemCount() { return (int32_t)wallMeshs.size(); }

void CalibWallMeshBuild::clear() { wallMeshs.clear(); }

const char* CalibWallMeshBuild::getLastError() { return lastError.c_str(); }

}  // namespace avox
