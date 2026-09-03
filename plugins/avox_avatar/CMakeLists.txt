# avox_avatar 插件: avatar 域驱动
#   - 音频->ARKit52 blendshape (wav2arkit): PCM→虚拟人口型, 业务走 audioFaceHub.create("wav2arkit")
#   - 视频->ARKit52 blendshape + 478 landmarks (mediapipe): RGBA 帧→视频驱动, 业务走 videoFaceHub.create("mediapipe")
# loadModule 时把上述工厂注册到 AvoxManager。仅依赖 avox 核心 (OnnxModelUser/RunTask/Observer 均在 avox.dll);
# onnxruntime 经 avox_onnx 插件运行期经 OnnxSessionCache 加载, 不在此显式链接。
# mediapipe 后端的姿态对齐 warp 用 OpenCV (cv::warpAffine + WARP_INVERSE_MAP), 故需 OpenCV。
# 模型文件 (wav2arkit_cpu.onnx + face_detector/landmarker/blendshape.onnx) 经 src/CMakeLists.txt
# 的 avox_copy_assets 部署到 assets/models/avatar/。
if(NOT AVOX_ENABLE_AVATAR)
  message(STATUS "avox_avatar plugin: 虚拟人未启用, 跳过")
  return()
endif()

find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
  message(STATUS "avox_avatar plugin: opencv 未找到, 跳过 (mediapipe 视频驱动需 opencv warp)")
  return()
endif()

register_plugin(avox_avatar DYNAMIC LIBS ${OpenCV_LIBRARIES})
