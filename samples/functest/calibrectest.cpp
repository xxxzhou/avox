/**
 * @file calibrectest.cpp
 * @brief 虚拟制片相机标定真实数据验证 (avox_calib 插件)
 *
 * 数据: aoce 虚拟制片现场录制 (LED 幕墙 Aruco 图案 + 光学追踪器位姿)
 *   <dir>/Record.json  — items[]: trackPose{rotatePos{pos,rotation(3x3行主序)}} + path
 *   <dir>/*.png        — 1920x1080 摄像机拍摄画面
 *
 * 3D 点集: 平面 LED 屏 Aruco 网格参数化生成 (8 列 x 5 行, cell(0,0) 为屏号字符
 * 不参与识别, markerId = 行*8+列, startId=0)。格子物理尺寸不知晓时任意,
 * 统一尺度误差被标定 scale 顶点/Tsai scale 列吸收, 不影响验证。
 * (弧形幕墙 ds1 的 3D 点依赖 FBX mesh, 待 avox_fbx/ILedMeshBuild 后接入)
 *
 * 用法: calibrectest [recordDir] [cellAspect] [meshDir]
 *   recordDir  录制数据目录 (默认 aocec assets record/2, 平面屏参数化 3D 点)
 *   cellAspect 格子宽高比 (默认 1, LED 拉伸铺屏时非 1)
 *   meshDir    幕墙 FBX 目录 (给出时经 avox_fbx + ILedMeshBuild 生成 3D 点,
 *              用于弧形幕墙数据, 如 aocec assets calibration/led)
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "avox/AvoxCalib.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxScene.h"
#include "avox/module/AvoxManager.hpp"

using namespace avox;

// ============ Record.json 迷你解析 (固定结构顺序扫描, 避免 STL 跨 DLL 堆问题) ============

static std::string readFileStr(const char* path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) return {};
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // 去掉 UTF-8 BOM
  if (content.size() >= 3 && (uint8_t)content[0] == 0xEF && (uint8_t)content[1] == 0xBB &&
      (uint8_t)content[2] == 0xBF) {
    content = content.substr(3);
  }
  return content;
}

// 从 from 起找 "key": 后的数字
static bool findNum(const std::string& s, size_t& from, const char* key, double& value) {
  size_t p = s.find(key, from);
  if (p == std::string::npos) return false;
  p = s.find(':', p + strlen(key));
  if (p == std::string::npos) return false;
  p++;
  value = strtod(s.c_str() + p, nullptr);
  from = p;
  return true;
}

static bool findStr(const std::string& s, size_t from, const char* key, std::string& value) {
  size_t p = s.find(key, from);
  if (p == std::string::npos) return false;
  p = s.find('"', p + strlen(key));
  if (p == std::string::npos) return false;
  p++;
  size_t e = s.find('"', p);
  if (e == std::string::npos) return false;
  value = s.substr(p, e - p);
  return true;
}

struct RecordItem {
  Mat4x4d pose;  // 行主序, row0-2 旋转, row3.xyz 平移 (与 avox Mat4x4d 同构)
  std::string imagePath;
};

static bool parseRecord(const std::string& json, std::vector<RecordItem>& items) {
  size_t pos = json.find("\"items\"");
  if (pos == std::string::npos) return false;
  // 按 "trackPose" 切块顺序解析
  while (true) {
    size_t tp = json.find("\"trackPose\"", pos);
    if (tp == std::string::npos) break;
    size_t next = json.find("\"trackPose\"", tp + 1);
    size_t chunkEnd = next != std::string::npos ? next : json.size();
    RecordItem item;
    double v = 0;
    size_t cur = tp;
    // pos x,y,z ("pos" 后是对象, 需先定位到 "x" 键再取数)
    size_t pp = json.find("\"pos\"", tp);
    if (pp == std::string::npos || pp > chunkEnd) break;
    cur = pp;
    bool ok = findNum(json, cur, "\"x\"", v);
    double px = v;
    ok = findNum(json, cur, "\"y\"", v) && ok;
    double py = v;
    ok = findNum(json, cur, "\"z\"", v) && ok;
    double pz = v;
    // rotation 3 行
    double rows[3][3] = {};
    const char* rowKeys[3] = {"\"row0\"", "\"row1\"", "\"row2\""};
    for (int r = 0; r < 3 && ok; r++) {
      size_t rp = json.find(rowKeys[r], tp);
      if (rp == std::string::npos || rp > chunkEnd) {
        ok = false;
        break;
      }
      cur = rp;
      ok = findNum(json, cur, "\"x\"", rows[r][0]);
      ok = findNum(json, cur, "\"y\"", rows[r][1]) && ok;
      ok = findNum(json, cur, "\"z\"", rows[r][2]) && ok;
    }
    if (!ok) break;
    Mat4x4d& m = item.pose;
    for (int r = 0; r < 3; r++) {
      vec4d& row = (r == 0) ? m.row0 : ((r == 1) ? m.row1 : m.row2);
      row = vec4d(rows[r][0], rows[r][1], rows[r][2], 0.0);
    }
    m.row3 = vec4d(px, py, pz, 1.0);
    // path
    if (!findStr(json, tp, "\"path\"", item.imagePath)) break;
    items.push_back(item);
    pos = chunkEnd;
  }
  return !items.empty();
}

// ============ 平面 LED 屏 Aruco 3D 点参数化 ============

static const int32_t kGridW = 8;     // 每屏 Aruco 列数
static const int32_t kGridH = 5;     // 每屏 Aruco 行数
static const double kCell = 0.5;     // 格子物理尺寸 (任意, 尺度被标定 scale 吸收)
static const double kGridPixel = 126.0;  // 每格 LED 像素
static const double kBits = 6.0;         // dict_6x6
// 格子宽高比 (LED 处理器把标定图拉伸铺满全屏时 ≠ 1: 全屏 16:9 下 8x5 格 = 240/216)
static double kCellAspect = 1.0;

// markerId m 的 4 角点位于 points[m*4 .. m*4+3] (左上,右上,右下,左下, OpenCV 序)
static void buildFlatLedPoints(std::vector<vec3f>& points) {
  // 边框像素 = gridPixel/(bits+2) 取整, inner = gridPixel - 2*board
  int32_t boardPixel = (int32_t)(kGridPixel / (kBits + 2.0));
  double boardX = kCell * kCellAspect * boardPixel / kGridPixel;
  double boardY = kCell * boardPixel / kGridPixel;
  double cellW = kCell * kCellAspect;
  double cellH = kCell;
  double innerX = cellW - 2.0 * boardX;
  double innerY = cellH - 2.0 * boardY;
  points.clear();
  points.reserve((size_t)(kGridW * kGridH * 4));
  for (int32_t j = 0; j < kGridH; j++) {
    for (int32_t i = 0; i < kGridW; i++) {
      double x0 = i * cellW + boardX;
      double y0 = j * cellH + boardY;
      // 左上,右上,右下,左下 (含 cell(0,0) 幽灵点: id0 为屏号字符不识别)
      points.push_back(vec3f((float)x0, (float)y0, 0.0f));
      points.push_back(vec3f((float)(x0 + innerX), (float)y0, 0.0f));
      points.push_back(vec3f((float)(x0 + innerX), (float)(y0 + innerY), 0.0f));
      points.push_back(vec3f((float)x0, (float)(y0 + innerY), 0.0f));
    }
  }
}

int main(int argc, char* argv[]) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* recordDir = argc > 1 ? argv[1] : "Q:/Work/github/aocec/assets/calibration/record/2";
  const char* meshDir = argc > 3 ? argv[3] : nullptr;
  if (argc > 2) kCellAspect = atof(argv[2]);
  printf("=== 虚拟制片标定真实数据验证 (avox_calib) ===\n  data: %s\n", recordDir);

  // 1. 解析录制数据
  std::string jsonPath = std::string(recordDir) + "/Record.json";
  std::string json = readFileStr(jsonPath.c_str());
  std::vector<RecordItem> records;
  if (json.empty() || !parseRecord(json, records)) {
    printf("FAIL 解析 Record.json 失败 (%s)\n", jsonPath.c_str());
    return 1;
  }
  printf("  解析到 %d 组 (画面+追踪器位姿)\n", (int32_t)records.size());

  // 2. 从 hub 创建标定对象 (create 内部加载 avox_calib 插件)
  std::unique_ptr<IImagePoints> points(AvoxManager::Get().imagePointsHub.create("opencv"));
  std::unique_ptr<ICameraCalibration> calibration(
      AvoxManager::Get().cameraCalibrationHub.create("opencv"));
  std::unique_ptr<ICameraOffset> offset(AvoxManager::Get().cameraOffsetHub.create("opencv"));
  std::unique_ptr<IVideoCalibration> video(AvoxManager::Get().videoCalibrationHub.create("opencv"));
  if (!points || !calibration || !offset || !video) {
    printf("FAIL 创建标定对象失败 (avox_calib 插件未加载?)\n");
    return 1;
  }
  std::vector<vec3f> pts3d;
  if (meshDir != nullptr) {
    // 弧形/异形幕墙: FBX mesh → Aruco 3D 点 (avox_fbx + ILedMeshBuild)
    std::unique_ptr<ISceneImport> sceneImport(
        AvoxManager::Get().sceneImportHub.create("fbx"));
    std::unique_ptr<ILedMeshBuild> meshBuild(
        AvoxManager::Get().ledMeshBuildHub.create("opencv"));
    if (!sceneImport || !meshBuild) {
      printf("FAIL 创建 fbx/meshBuild 失败 (avox_fbx 插件未加载?)\n");
      return 1;
    }
    std::vector<std::string> fbxs;
    for (auto& entry : std::filesystem::directory_iterator(meshDir)) {
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      // 过滤其它数据集的模型 (如 led_4.fbx 属平面屏 428 数据)
      if (ext == ".fbx" && entry.path().filename().string().find("led") != 0) {
        fbxs.push_back(entry.path().string());
      }
    }
    std::sort(fbxs.begin(), fbxs.end());
    for (const auto& fbx : fbxs) {
      if (!sceneImport->open(fbx.c_str())) {
        printf("  加载失败: %s (%s)\n", fbx.c_str(), sceneImport->getLastError());
        continue;
      }
      for (int32_t i = 0; i < sceneImport->getMeshCount(); i++) {
        meshBuild->loadMesh(sceneImport->getMesh(i), 216, 216);
      }
    }
    if (meshBuild->getItemCount() <= 0) {
      printf("FAIL meshDir 无有效幕墙 mesh\n");
      return 1;
    }
    printf("  幕墙 mesh 块数: %d\n", meshBuild->getItemCount());
    meshBuild->updateWall(CalibArucoType::dict6x6_1000, 0);
    meshBuild->createArucoPoints(points.get());
    meshBuild->saveArucoImage(0, "calibrec_atlas0.png");
    // 调试: 导出 markerId → 3D 点表 (python 对照实验用)
    {
      FILE* fp = fopen("calibrec_points.txt", "w");
      int32_t count = points->getPointCount();
      for (int32_t k = 0; k < count; k++) {
        vec3f p = points->getPoint(k);
        char lineBuf[128];
        snprintf(lineBuf, sizeof(lineBuf), "%d %.6f %.6f %.6f", k / 4, p.x, p.y, p.z);
        fputs(lineBuf, fp);
        fputc(10, fp);
      }
      fclose(fp);
    }
  } else {
    buildFlatLedPoints(pts3d);
    points->setArucoInfo(CalibArucoType::dict6x6_1000, 0);
    points->setPoints((int32_t)pts3d.size(), pts3d.data());
  }
  printf("  3D 点集: %d 点 (%dx%d Aruco 网格)\n",
         meshDir != nullptr ? points->getPointCount() : (int32_t)pts3d.size(), kGridW, kGridH);

  vec2d imgSize = vec2d(1920.0, 1080.0);
  calibration->setImagePoints(imgSize, points.get());
  video->setImagePoints(points.get());

  // 3. 逐帧喂入内参标定与序列标定 (手眼在 4 拿到内参后重喂)
  std::vector<std::unique_ptr<IImageBuffer>> images;
  std::vector<int32_t> validIdx;  // image 索引 → record 索引
  LensModel lensModel;
  for (size_t i = 0; i < records.size(); i++) {
    std::string imgPath = std::string(recordDir) + "/" + records[i].imagePath;
    images.emplace_back(createImageBuffer());
    if (!loadImagePath(imgPath.c_str(), images.back().get())) {
      printf("FAIL 加载图片: %s\n", imgPath.c_str());
      return 1;
    }
    int32_t n = calibration->saveCornerImage(images.back().get());
    int32_t vn = video->saveImage(images.back().get());
    video->saveTrackPose(records[i].pose);
    if (n < 0 && vn < 0) {
      printf("  帧 %d 未识别到 Aruco (%s)\n", (int32_t)i, records[i].imagePath.c_str());
      continue;
    }
    validIdx.push_back((int32_t)i);
  }
  printf("  有效标定帧: %d\n", (int32_t)validIdx.size());
  if (validIdx.size() < 3) {
    printf("FAIL 有效帧不足 3\n");
    return 1;
  }

  // 4. 内参标定 (初值: fx/fy 比例 0.9/1.6, 同 aoce 原版测试)
  lensModel.imageSize = imgSize;
  lensModel.focalLength = vec2d(0.9, 1.6);
  float innerErr = calibration->calibration(lensModel, true);
  if (meshDir != nullptr && (innerErr < 0 || innerErr > 20.0f)) {
    // 弧形(非平面)标定物: calibrateCamera 发散 (aoce 因此引入 g2o 内参 BA, 属 M2),
    // M1 改用近似物理焦距(无畸变)直接验证 PnP+手眼链路
    lensModel = LensModel();
    lensModel.imageSize = imgSize;
    lensModel.focalLength = vec2d(0.9, 1.6);
    innerErr = -1.0f;
    printf("  (非平面内参发散, 改用近似焦距继续验证手眼链路)\n");
  }
  double fx = lensModel.focalLength.x * imgSize.x;
  double fy = lensModel.focalLength.y * imgSize.y;
  double fovX = getLensFovX(lensModel) * 180.0 / 3.14159265358979323846;
  printf("  内参: err=%.3fpx fx=%.1f fy=%.1f center=(%.3f,%.3f) fovX=%.1f°\n", innerErr, fx, fy,
         lensModel.focalCenter.x, lensModel.focalCenter.y, fovX);
  printf("  畸变: k1=%.5f k2=%.5f p1=%.5f p2=%.5f k3=%.5f\n", lensModel.k1, lensModel.k2,
         lensModel.p1, lensModel.p2, lensModel.k3);

  // 5. 手眼标定 (bScale=true: 追踪器位移存在未知缩放; setLensModel 会 reset, 重喂有效帧)
  offset->setLensModel(lensModel, points.get());
  for (size_t k = 0; k < validIdx.size(); k++) {
    int32_t ri = validIdx[k];
    offset->saveTrackCornerImage(records[ri].pose, images[k].get());
  }
  OffsetParamet paramet;
  paramet.startIndex = 3;
  paramet.computeRange = 2;
  paramet.bScale = true;
  CameraTrackOffset result;
  int32_t used = offset->compute(paramet, result);
  const CameraTrack& ct = result.cameraTrack;
  printf("  手眼: used=%d 平均重投影误差=%.3fpx scale=%.4f\n", used, result.offset, ct.scale);
  printf("  camera2track 平移=(%.4f, %.4f, %.4f)m\n", ct.camera2track.row3.x, ct.camera2track.row3.y,
         ct.camera2track.row3.z);
  printf("  suggest 位掩码=0x%llx\n", (unsigned long long)result.suggest);
  // 逐帧 Track 误差
  double totalErr = 0;
  int32_t errCount = 0;
  for (int32_t i = 0; i < used; i++) {
    FrameOffset trackOff, innerOff;
    if (!offset->getFrameOffset(i, trackOff, innerOff)) continue;
    printf("  帧%d: 内参误差=%.2fpx Track误差=%.2fpx\n", i, innerOff.offset.avg, trackOff.offset.avg);
    totalErr += trackOff.offset.avg;
    errCount++;
  }
  if (errCount > 0) {
    printf("  Track 平均误差=%.3fpx %s\n", totalErr / errCount,
           totalErr / errCount < 10.0 ? "(10px 内, 可现场使用)" : "(需检查数据/追踪器)");
  }

  // 6. 序列一站式标定 (交叉验证)
  VideoRTParamet rtPar;
  rtPar.minBlur = 0.1f;
  rtPar.minVisibility = 0.2f;
  VideoResult videoResult;
  if (video->compute(rtPar, videoResult)) {
    printf("  序列标定: 内参误差=%.3fpx 手眼误差=%.3fpx scale=%.4f\n", videoResult.lensOffset,
           videoResult.offsetResult.offset, videoResult.offsetResult.cameraTrack.scale);
  } else {
    printf("  序列标定失败: %s\n", video->getLastError());
  }

  printf("\n完成。\n");
  return 0;
}
