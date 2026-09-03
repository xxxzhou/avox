# avox_onnx 插件: ONNX Runtime 推理引擎
# loadModule 时把 ONNXSession 工厂注册到 AvoxManager, OnnxTranslator 走 onnxSessionHub.create("onnx") 查表
# 注: IONNXSession 纯接口在核心 avox/vision/, ONNXSession 实现在此 plugin, 核心不再 include onnxruntime
if(NOT AVOX_ENABLE_ONNX)
  message(STATUS "avox_onnx plugin: ONNX 未启用, 跳过")
  return()
endif()

find_package(ONNX QUIET)
if(NOT ONNX_FOUND)
  message(STATUS "avox_onnx plugin: onnxruntime 库未找到, 跳过")
  return()
endif()

# onnxruntime 头目录由 AVOXOptions 全局 include_directories(${ONNXRUNTIME_INCLUDE_DIRS}) 提供, 子目录继承
register_plugin(avox_onnx
  DYNAMIC
  LIBS ${ONNXRUNTIME_LIBRARIES}
  DEP_DLLS ${ONNXRUNTIME_DLLS})
# onnxruntime 正常链接 (普通隐式依赖), 与 avox_openvino/avox_opencv 一致。
# 曾用 /DELAYLOAD 试图避免 DllMain 阶段加载, 但 delayimp 的 __delayLoadHelper2 在插件加载时
# 即触发, onnxruntime DllMain 线程初始化在部分加载上下文下崩溃 (err=1114)。正常链接让加载器
# 统一处理, 无需 AddDllDirectory 等 workaround。
#
# 注意: onnxruntime.dll 必须使用静态链 VC 运行时的版本 (15MB, 无 MSVCP140 依赖);
# 微软官方 CPU release (14MB, 动态链 MSVCP140) 在 plugins/ 目录下 DllMain 初始化失败 (err=1114)。
