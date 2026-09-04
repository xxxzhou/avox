#pragma once

// FBX 场景导入 (ufbx 单文件解析器, https://github.com/ufbx/ufbx)
// 每个 FBX 节点网格展开为三角 Corner 对齐的 position/uv 数组 (应用节点世界变换,
// 位置保持 FBX 原生单位 cm)

#include <memory>
#include <string>
#include <vector>

#include "avox/AvoxScene.h"

namespace avox {

class FbxMeshData : public ISceneMesh {
  friend class FbxSceneImport;

 private:
  std::string name;
  std::vector<vec3f> positions;
  std::vector<vec2f> uvs;
  std::vector<vec3i> triangles;

 public:
  virtual const char* getName() override { return name.c_str(); }
  virtual int32_t getVertexCount() override { return (int32_t)positions.size(); }
  virtual const vec3f* getPositions() override { return positions.data(); }
  virtual const vec2f* getUVs() override { return uvs.data(); }
  virtual int32_t getTriangleCount() override { return (int32_t)triangles.size(); }
  virtual const vec3i* getTriangles() override { return triangles.data(); }
};

class FbxSceneImport : public ISceneImport {
 private:
  void* scene = nullptr;  // ufbx_scene* (避免头文件泄漏 ufbx.h)
  std::vector<std::unique_ptr<FbxMeshData>> meshes;
  std::string lastError;

 public:
  FbxSceneImport() = default;
  virtual ~FbxSceneImport() override;

 public:
  virtual bool open(const char* filePath) override;
  virtual void close() override;
  virtual int32_t getMeshCount() override;
  virtual ISceneMesh* getMesh(int32_t index) override;
  virtual const char* getLastError() override;
};

}  // namespace avox
