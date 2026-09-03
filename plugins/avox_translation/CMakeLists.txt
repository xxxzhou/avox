# avox_translation 插件: 离线翻译(opus-mt, onnx + sentencepiece)
# loadModule 注册 OnnxTranslator 工厂到 AvoxManager, SubtitleAsr 走 translatorHub.create 查表
# 依赖 avox_onnx plugin(OnnxTranslator::load 走 onnxSessionHub.create 拿 IONNXSession 实例, dependsOn 声明)
#
# HttpTranslator(腾讯云 http)已迁 src/avox_zlmediakit/(依赖 mk_httpclient, 内置模块内才链得到),
# 工厂由 AvoxManager::init() 中 regHttpTranslator() 注册, SubtitleAsr 查 "http" 可拿到实例
if(NOT AVOX_ENABLE_TRANSLATION)
  message(STATUS "avox_translation plugin: translation 未启用, 跳过")
  return()
endif()

find_package(ONNX QUIET)
find_package(SentencePiece QUIET)
if(NOT (ONNX_FOUND AND SentencePiece_FOUND))
  message(STATUS "avox_translation plugin: onnx/sentencepiece 库未找到, 跳过")
  return()
endif()

register_plugin(avox_translation
  DYNAMIC
  SOURCES OnnxTranslator.cpp TranslationHelper.cpp TranslationModule.cpp
  LIBS ${SPM_LIBRARIES})
