# avox_cv 插件: 集中收纳 onnx+opencv 相关 CV 能力
#   - 水印去除 (YOLO-Seg 检测 + LaMa/AOT-GAN 修复): 业务走 watermarkRemovalHub.create("inpaint")
#   - 通用 YOLO (Ultralytics 检测/分类): 业务走 yoloDetectorHub.create("yolo")
# loadModule 把上述工厂注册到 AvoxManager。依赖 avox_onnx(走 onnxSessionHub.create 拿 IONNXSession) +
#        avox_opencv(cv::Mat 预处理/后处理, 能力探测) + OpenCV + ONNX Runtime
if(NOT AVOX_ENABLE_CV)
  message(STATUS "avox_cv plugin: cv 未启用, 跳过")
  return()
endif()

find_package(ONNX QUIET)
find_package(OpenCV QUIET)
if(NOT (ONNX_FOUND AND OpenCV_FOUND))
  message(STATUS "avox_cv plugin: onnx/opencv 库未找到, 跳过")
  return()
endif()

register_plugin(avox_cv
  DYNAMIC
  DEPS avox_onnx avox_opencv
  SOURCES
    CvModule.cpp
    core/WatermarkRemoval.cpp
    core/MaskProcessor.cpp
    detect/YOLODetector.cpp
    yolo/YoloDetector.cpp
    inpaint/LamaInpainter.cpp
    inpaint/AOTGanInpainter.cpp
    inpaint/TiledInpainter.cpp
  LIBS ${OpenCV_LIBRARIES})
