# avox_fbx 插件: FBX 场景解析 (ufbx 单文件解析器, 无外部依赖)
# 供虚拟制片标定 (avox_calib ILedMeshBuild) 与通用模型网格读取使用
if(NOT AVOX_ENABLE_FBX)
  message(STATUS "avox_fbx plugin: 未启用, 跳过")
  return()
endif()

register_plugin(avox_fbx
  DYNAMIC
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/FbxModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FbxSceneImport.cpp
    ${CMAKE_SOURCE_DIR}/3rdparty/ufbx/ufbx.c
    ${CMAKE_SOURCE_DIR}/3rdparty/ufbx/ufbx.h)

# ufbx.h 位于 3rdparty/ufbx
target_include_directories(avox_fbx PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/ufbx)
