#include "FbxSceneImport.hpp"

#include <memory>
#include <ufbx.h>

#include "avox/module/LogHelper.hpp"

namespace avox {

FbxSceneImport::~FbxSceneImport() { close(); }

void FbxSceneImport::close() {
  if (scene) {
    ufbx_free_scene((ufbx_scene*)scene);
    scene = nullptr;
  }
  meshes.clear();
}

bool FbxSceneImport::open(const char* filePath) {
  close();
  ufbx_error error;
  // FBX 原生单位 cm, 不做单位转换 (标定侧自行 0.01 转米)
  ufbx_load_opts opts = {0};
  ufbx_scene* sc = ufbx_load_file(filePath, &opts, &error);
  if (!sc) {
    lastError = std::string("ufbx load failed: ") + error.description.data;
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  scene = sc;
  // 遍历全部节点, 提取网格 (按三角 Corner 展开位置/UV)
  for (size_t ni = 0; ni < sc->nodes.count; ni++) {
    ufbx_node* node = sc->nodes.data[ni];
    if (!node->mesh) continue;
    ufbx_mesh* mesh = node->mesh;
    if (mesh->num_faces <= 0) continue;
    auto meshData = std::make_unique<FbxMeshData>();
    meshData->name = node->name.data ? node->name.data : "";
    // 节点几何→世界变换 (ufbx_matrix: 变换 = cols[0]*x + cols[1]*y + cols[2]*z + cols[3])
    ufbx_matrix geo = node->geometry_to_world;
    // 逐面三角化, 三角索引为 corner 下标, position/uv 逐 corner 展开
    std::vector<uint32_t> triIndices(3 * (size_t)mesh->max_face_triangles + 3);
    for (size_t fi = 0; fi < mesh->num_faces; fi++) {
      ufbx_face face = mesh->faces.data[fi];
      if (face.num_indices < 3) continue;
      uint32_t numTri = ufbx_triangulate_face(triIndices.data(), face.num_indices, mesh, face);
      for (uint32_t t = 0; t < numTri; t++) {
        uint32_t a = triIndices[t * 3 + 0];
        uint32_t b = triIndices[t * 3 + 1];
        uint32_t c = triIndices[t * 3 + 2];
        meshData->triangles.push_back(vec3i((int32_t)a, (int32_t)b, (int32_t)c));
      }
    }
    size_t cornerCount = mesh->num_indices;
    meshData->positions.resize(cornerCount);
    meshData->uvs.resize(cornerCount);
    for (size_t ci = 0; ci < cornerCount; ci++) {
      ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ci);
      ufbx_vec3 tp = ufbx_transform_position(&geo, p);
      ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ci);
      meshData->positions[ci] = vec3f((float)tp.x, (float)tp.y, (float)tp.z);
      meshData->uvs[ci] = vec2f((float)uv.x, (float)uv.y);
    }
    LOGFLF(LogLevel::info, "fbx mesh: ", meshData->name.c_str(),
           " corners: ", (int32_t)cornerCount, " tris: ", (int32_t)meshData->triangles.size());
    meshes.push_back(std::move(meshData));
  }
  if (meshes.empty()) {
    lastError = "no mesh in fbx scene";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  return true;
}

int32_t FbxSceneImport::getMeshCount() { return (int32_t)meshes.size(); }

ISceneMesh* FbxSceneImport::getMesh(int32_t index) {
  if (index < 0 || index >= (int32_t)meshes.size()) return nullptr;
  return meshes[index].get();
}

const char* FbxSceneImport::getLastError() { return lastError.c_str(); }

}  // namespace avox
