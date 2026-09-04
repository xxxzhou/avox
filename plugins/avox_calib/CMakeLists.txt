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
