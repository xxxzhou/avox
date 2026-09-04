#pragma once

// ============ 场景模型导入 (FBX 等格式, 实现: plugins/avox_fbx) ============
// 用途: 虚拟制片 LED 幕墙标定 (AvoxCalib.h ILedMeshBuild 消费幕墙 mesh 生成
// Aruco 3D 点), 以及通用的模型网格读取。
// 跨 DLL 安全: 接口只返回原始数组指针/POD, 不传 STL (范本 AvoxVision.h)。

#include "AvoxDef.h"
#include "AvoxMath.h"

namespace avox {

// 场景网格 (只读, 三角网格, 位置单位 cm 即 FBX 原生单位)
class ISceneMesh {
 public:
  virtual ~ISceneMesh() = default;

 public:
  // 节点名 (utf8)
  virtual const char* getName() = 0;
  // 顶点数与逐顶点数据 (position/uv 已按三角 Corner 展开对齐)
  virtual int32_t getVertexCount() = 0;
  virtual const vec3f* getPositions() = 0;
  virtual const vec2f* getUVs() = 0;
  // 三角形数与索引 (指向 positions/uvs 下标)
  virtual int32_t getTriangleCount() = 0;
  virtual const vec3i* getTriangles() = 0;
};

// 场景导入 (open 后经 getMesh 遍历全部网格)
class ISceneImport {
 public:
  virtual ~ISceneImport() = default;

 public:
  virtual bool open(const char* filePath) = 0;
  virtual void close() = 0;
  virtual int32_t getMeshCount() = 0;
  // 生命周期归 ISceneImport 管理, close/析构后失效
  virtual ISceneMesh* getMesh(int32_t index) = 0;
  virtual const char* getLastError() = 0;
};

}  // namespace avox
