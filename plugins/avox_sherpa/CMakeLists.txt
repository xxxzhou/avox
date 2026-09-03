# avox_sherpa 插件: sherpa-onnx 流式语音识别
# loadModule 时把 SherpaAudioStt 工厂注册到 AvoxManager, SubtitleAsr 走 audioSttHub.create("sherpa") 查表
if(NOT AVOX_ENABLE_SHERPA)
  message(STATUS "avox_sherpa plugin: sherpa-onnx 未启用, 跳过")
  return()
endif()

find_package(SherpaOnnx QUIET)
if(NOT SherpaOnnx_FOUND)
  message(STATUS "avox_sherpa plugin: sherpa-onnx 库未找到, 跳过")
  return()
endif()

register_plugin(avox_sherpa
  DYNAMIC
  LIBS ${SHERPA_LIBRARYS}
  DEP_DLLS ${SHERPA_DLLS})
