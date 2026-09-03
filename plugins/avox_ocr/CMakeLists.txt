# avox_ocr 插件: OCR 文字识别 (PP-OCRv6 det + rec)
# loadModule 注册 TextRecognizer 工厂到 AvoxManager, 业务走 textRecognizerHub.create("ppocr") 查表
# 依赖 avox_onnx(TextRecognizer 经 onnxSessionHub.create("onnx") 拿 IONNXSession, 运行期查表解耦) +
#        OpenCV(cv::Mat 预处理/后处理)
# 注: avox_ocr 不直接 link onnxruntime (经 IONNXSession 接口), 故只 find OpenCV
if(NOT AVOX_ENABLE_OCR)
  message(STATUS "avox_ocr plugin: OCR 未启用, 跳过")
  return()
endif()
if(NOT AVOX_ENABLE_ONNX)
  message(STATUS "avox_ocr plugin: 需 AVOX_ENABLE_ONNX (提供 IONNXSession), 跳过")
  return()
endif()

find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
  message(STATUS "avox_ocr plugin: OpenCV 库未找到, 跳过")
  return()
endif()

register_plugin(avox_ocr
  DYNAMIC
  DEPS avox_onnx
  SOURCES
    OcrModule.cpp
    TextRecognizer.cpp
  LIBS ${OpenCV_LIBRARIES})
