# avox_webrtc 插件: WebRTC 推拉流 + 3A 音频处理
# loadModule 注册 RtcPlayer/AudioProcess/RawSource 工厂到 AvoxManager,
# 核心层走 rtcPlayerHub.create("webrtc") / audioProcessHub.create("webrtc") 查表
if(NOT AVOX_ENABLE_WEBRTC)
  message(STATUS "avox_webrtc plugin: WebRTC 未启用, 跳过")
  return()
endif()

find_package(WebRTC QUIET)
if(NOT WebRTC_FOUND)
  message(STATUS "avox_webrtc plugin: WebRTC 库未找到, 跳过")
  return()
endif()

# WebRTC 平台链接库
set(WEBRTC_PLATFORM_LIBS "")
if(WIN32)
  list(APPEND WEBRTC_PLATFORM_LIBS winmm wmcodecdspuuid mfplat dmoguids msdmo iphlpapi)
endif()
if(ANDROID)
  # webrtc android 侧依赖的系统库 (log/native window/audio/图形)
  list(APPEND WEBRTC_PLATFORM_LIBS log android EGL GLESv2 OpenSLES)
endif()

register_plugin(avox_webrtc
  DYNAMIC
  SOURCES
    WebrtcModule.cpp
    RtcEngine.cpp
    RtcHelper.cpp
    RtcParse.cpp
    RtcPlayer.cpp
    audio/RtcAudioDecoder.cpp
    audio/RtcAudioDecoderFactory.cpp
    audio/RtcAudioEncoder.cpp
    audio/RtcAudioEncoderFactory.cpp
    audio/RtcAudioProcess.cpp
    audio/RtcAudioSource.cpp
    video/RtcVideoBuffer.cpp
    video/RtcVideoDecoder.cpp
    video/RtcVideoDecoderFactory.cpp
    video/RtcVideoEncoder.cpp
    video/RtcVideoEncoderFactory.cpp
    video/RtcVideoSource.cpp
  LIBS ${WEBRTC_LIBRARIES} ${WEBRTC_PLATFORM_LIBS})

# MSVC STL ABI 兼容性 shim (webrtc.lib 用 MSVC 14.44+ 编译，当前 MSVC 14.43 缺少符号)
if(WIN32 AND MSVC AND MSVC_VERSION LESS 1944)
  target_sources(avox_webrtc PRIVATE msvc1444/msvc_stl_shim.cpp)
endif()
