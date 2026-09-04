# avox_calib 插件: 虚拟制片相机标定 (内参/手眼+scale/PnP/序列标定, 移植自 aoce)
# 依赖 opencv_world (calib3d + objdetect/aruco), 链接 + dll 复制到 plugins/ 自包含
if(NOT AVOX_ENABLE_CALIB)
  message(STATUS "avox_calib plugin: 未启用, 跳过")
  return()
endif()
if(NOT AVOX_ENABLE_OPENCV OR NOT OpenCV_FOUND)
  message(STATUS "avox_calib plugin: OpenCV 未启用/未找到, 跳过")
  return()
endif()

# 选 opencv dll(按 AVOX_DEBUG), 复制到 plugins/ 让 LoadLibraryEx 能从同目录找到依赖
if(WIN32)
  if(AVOX_DEBUG AND OpenCV_DLLS_DEBUG)
    set(_ocv_dlls ${OpenCV_DLLS_DEBUG})
  elseif(NOT AVOX_DEBUG AND OpenCV_DLLS_RELEASE)
    set(_ocv_dlls ${OpenCV_DLLS_RELEASE})
  endif()
endif()

register_plugin(avox_calib
  DYNAMIC
  LIBS ${OpenCV_LIBRARIES}
  DEP_DLLS ${_ocv_dlls})

# ============ g2o 图优化 (M2, 可选) ============
# 弧形(非平面)幕墙内参 BA / 手眼再优化; 源码 submodule + build_windows.py 构建静态库
# 未构建/未启用时自动降级 M1 OpenCV 路径 (optimizer hub create 返回 nullptr)
if(AVOX_ENABLE_G2O)
  set(_g2o_root ${CMAKE_SOURCE_DIR}/3rdparty/g2o)
  if(AVOX_DEBUG)
    set(_g2o_bin ${_g2o_root}/bin/Debug)
    set(_g2o_postfix "d")
  else()
    set(_g2o_bin ${_g2o_root}/bin/Release)
    set(_g2o_postfix "")
  endif()
  foreach(_mod core stuff types_sba types_slam3d solver_dense solver_eigen)
    if(NOT EXISTS "${_g2o_bin}/g2o_${_mod}${_g2o_postfix}.lib")
      set(_g2o_missing ${_mod})
      break()
    endif()
  endforeach()
  if(NOT DEFINED _g2o_missing)
    target_compile_definitions(avox_calib PRIVATE
      AVOX_ENABLE_G2O
      G2O_USE_VENDORED_CERES)  # base_fixed_sized_edge 走 g2o 自带 ceres 头
    target_include_directories(avox_calib PRIVATE
      ${CMAKE_SOURCE_DIR}/3rdparty/eigen
      ${_g2o_root}
      ${_g2o_root}/EXTERNAL  # g2o 自带 ceres 头 (base_fixed_sized_edge 用)
      ${CMAKE_SOURCE_DIR}/build/windows/g2o)  # 生成的 g2o/config.h
    target_link_libraries(avox_calib PRIVATE
      ${_g2o_bin}/g2o_core${_g2o_postfix}.lib
      ${_g2o_bin}/g2o_stuff${_g2o_postfix}.lib
      ${_g2o_bin}/g2o_types_sba${_g2o_postfix}.lib
      ${_g2o_bin}/g2o_types_slam3d${_g2o_postfix}.lib
      ${_g2o_bin}/g2o_solver_dense${_g2o_postfix}.lib
      ${_g2o_bin}/g2o_solver_eigen${_g2o_postfix}.lib)
    message(STATUS "avox_calib: g2o 图优化已启用")
  else()
    message(STATUS "avox_calib: g2o 未构建 (缺 ${_g2o_missing}), 走 M1 OpenCV 路径")
  endif()
endif()
