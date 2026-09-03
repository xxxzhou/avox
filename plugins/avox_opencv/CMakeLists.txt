# avox_opencv 插件: OpenCV 图像处理能力(cv::Mat <-> IImageBuffer)
# 依赖第三方库 opencv_world(链接 + dll 复制到 plugins/, 使 plugins/ 自包含)
if(NOT AVOX_ENABLE_OPENCV)
  message(STATUS "avox_opencv plugin: OpenCV 未启用, 跳过")
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

register_plugin(avox_opencv
  DYNAMIC
  LIBS ${OpenCV_LIBRARIES}
  DEP_DLLS ${_ocv_dlls})
