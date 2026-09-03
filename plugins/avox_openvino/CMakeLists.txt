# avox_openvino 插件: OpenVINO 推理引擎 (Intel iGPU/CPU)
# loadModule 时把 OVEngine 工厂注册到 AvoxManager, VkQEnhanceLayer 走 openvinoEngineHub.create("openvino")
# IOVEngine 纯接口在核心 avox/vision/, OVEngine 实现在此 plugin, 核心不 include openvino
if(NOT AVOX_ENABLE_OPENVINO)
  message(STATUS "avox_openvino plugin: OpenVINO 未启用, 跳过")
  return()
endif()

find_package(OpenVINO QUIET)
if(NOT OpenVINO_FOUND)
  message(STATUS "avox_openvino plugin: openvino 库未找到, 跳过 (VkQEnhanceLayer 将走 ORT CPU)")
  return()
endif()

# openvino 头目录由 AVOXOptions 全局 include_directories(${OPENVINO_INCLUDE_DIRS}) 提供, 子目录继承
register_plugin(avox_openvino
  DYNAMIC
  LIBS ${OPENVINO_LIBRARIES}
  DEP_DLLS ${OPENVINO_DLLS})
# openvino 延迟加载: 避免 DllMain 阶段 OpenVINO/TBB 初始化异常(同 onnxruntime 教训);
# 实际 API 调用时才加载 openvino.dll, 此时 CRT 已稳定
if(MSVC)
  # 不用 /DELAYLOAD: delayimp 的 LoadLibrary("openvino.dll") 按 DLL search path 查找,
  # exe 在 install/.../Release/ 而 openvino.dll 在 Release/plugins/, 不在 search path →
  # 首次 ov 调用(new OVEngine 触发 ov::InferRequest 成员构造)解析失败 → 调用空指针 crash。
  # (诊断确认: crash 卡在 create("openvino")=new OVEngine(), loadModel 的 try-catch 在其后, 接不住)
  # 隐式依赖: 加载 avox_openvino.dll 时自动从其所在目录(plugins/)加载 openvino.dll 及依赖(tbb 等)。
  # /EHa: 若 openvino GPU init 触发 SEH, catch(...) 能接住降级 CPU/ORT。
  target_compile_options(avox_openvino PRIVATE /EHa)
endif()
