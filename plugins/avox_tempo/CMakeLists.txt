# avox_tempo 插件: 音频变速不变调 (SoundTouch WSOLA)
# loadModule 时把 IAudioTempo 工厂注册到 AvoxManager.audioTempoHub("soundtouch"),
# ARenderTask 在 speed!=1 时 create 接入播放管线; 未装本插件时核心降级变调重采样。
#
# SoundTouch 引用方式与 libsmb2 一致: 独立源码仓预编译进库仓
#   源项目: D:/Work/github/soundtouch (本仓 script/soundtouch/build_windows.py, checkout 2.4.1)
#   产物:   ../avc_library/3rdparty/library/<platform>/soundtouch/{include,lib}
# 由 cmake/FindSoundTouch.cmake 解析; 未就位时本插件整体跳过
if(NOT AVOX_ENABLE_TEMPO)
  message(STATUS "avox_tempo plugin: 未启用, 跳过")
  return()
endif()
find_package(SoundTouch QUIET)
if(NOT SoundTouch_FOUND)
  message(STATUS "avox_tempo plugin: soundtouch 未找到, 跳过(见 D:/Work/github/soundtouch)")
  return()
endif()

include_directories(${SOUNDTOUCH_INCLUDE_DIRS})

# 源文件由 register_plugin 自动 glob, 静态库 /MT 与插件 CRT 一致(见库仓构建脚本)
register_plugin(avox_tempo DYNAMIC LIBS ${SOUNDTOUCH_LIBRARIES})
