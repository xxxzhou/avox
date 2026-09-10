# avoxtest: Apple 平台功能矩阵测试 (iOS 真机 app / macOS 无头 CLI)
#
# 前置: 先产出 SDK 安装目录 (include/ + 静态库), 例:
#   macOS: python build_mac.py    -> build/macos/avox/install/aarch64
#   iOS:   按平台脚本产出 install (ios) 或沿用既有 sdk-ios 布局
#
# iOS 真机 (签名走 Xcode GUI, CLI 只做编译验证):
#   cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
#     -DCMAKE_OSX_ARCHITECTURES=arm64 \
#     -DAVOX_LIB_DIR=<install>/Release -DAVOX_INCLUDE_DIR=<install>/include \
#     -DAVOX_WEBRTC_LIB=<avc_library>/build/ios/release/libwebrtc_nosym.a ..
#   xcodebuild/Xcode 点 Run
#
# macOS 无头 (跑完退出, 退出码 0=全过):
#   cmake -B build -DAVOX_LIB_DIR=<install>/aarch64/Release \
#     -DAVOX_INCLUDE_DIR=<install>/include \
#     -DAVOX_WEBRTC_LIB=<avc_library>/build/darwin/release/libwebrtc_nosym.a ..
#   cmake --build build && ./build/avoxtest
#
# 运行模式 (avoxtest.mm 内 AVOX_MATRIX/AVOX_IO_PLAN 环境变量):
#   默认: LAN 矩阵 — 依次拉 Windows ZLM (192.168.68.245) 的
#         rtsp-h264/rtsp-h265/rtmp-h264/hls-h265/ts-h264/webrtc-h264
#   AVOX_MATRIX=loop: 进程内回环 (起 ZLM 服务端 + mk_media 节流推流 + 回拉),
#         需 ffmpeg 头 (-DAVOX_FFMPEG_INCLUDE=...) 和测试源 wall_long/wall_265.mp4
#   启动参数带 "://": 单 URL 拉流模式
cmake_minimum_required(VERSION 3.20)
project(avoxtest LANGUAGES C CXX OBJC OBJCXX)

# avoxtest.mm 按 ARC 语义编写; CMake 生成的工程默认 MRC, autorelease 全局
# 在 iOS runloop 排干后悬空 (macOS 因主 runloop 永不返回侥幸不炸)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/avoxtest.mm"
  PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

set(TEAM_ID "Y96Y9F6CFS" CACHE STRING "Apple 开发者团队 ID")
# ZLM 头路径默认取本仓库 3rdparty (mk_mediakit.h + Common/config.h 事件名自检)
get_filename_component(AVOX_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(ZLM_DIR "${AVOX_REPO_ROOT}/3rdparty/ZLMediaKit")

# SDK 公共头: 直接引用源码树各模块目录 (install 目录不含 include)
function(avoxtest_include_dirs)
  target_include_directories(avoxtest PRIVATE
    "${ZLM_DIR}/src"
    "${ZLM_DIR}/3rdpart/ZLToolKit/src"
    "${ZLM_DIR}/api/include")
  foreach(d avox avox_apple avox_ffmpeg avox_vulkan avox_zlmediakit avox_freetype
           avox_egl avox_agent)
    target_include_directories(avoxtest PRIVATE "${AVOX_REPO_ROOT}/src/${d}")
  endforeach()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  # ── iOS 真机 app: 静态链全部 SDK 依赖 ──
  set(AVOX_LIB_DIR "${AVOX_LIB_DIR}" CACHE PATH "目录: libavox.a/libavcodec.a/...")
  set(AVOX_WEBRTC_LIB "${AVOX_WEBRTC_LIB}" CACHE FILEPATH "libwebrtc_nosym.a (avc_library ios 产物)")
  set(AVOX_LIBEXT_CODEC "${ZLM_DIR}/release/ios/Release/Release/libext-codec.a" CACHE FILEPATH "ZLM ext-codec")
  add_executable(avoxtest MACOSX_BUNDLE avoxtest.mm)
  avoxtest_include_dirs()
  # 摘掉 vulkan INTERFACE 传染来的 -Wl,-ld_classic: 旧链接器在本工程多档案
  # force_load 静态链下会丢 ZLM/ZLToolKit 的 const std::string 事件名初始化
  get_target_property(AVOXTEST_LINK_OPTIONS avoxtest LINK_OPTIONS)
  if(AVOXTEST_LINK_OPTIONS)
    list(REMOVE_ITEM AVOXTEST_LINK_OPTIONS "-Wl,-ld_classic")
    set_target_properties(avoxtest PROPERTIES LINK_OPTIONS "${AVOXTEST_LINK_OPTIONS}")
  endif()
  # Xcode Release 默认 dead_strip 会剥掉 force_load 多档案静态链下的
  # __mod_init_func (iOS 实测 0 条) → ZLM 事件名初始化丢失, 关闭死代码剥离
  set_target_properties(avoxtest PROPERTIES XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING "NO")
  target_link_libraries(avoxtest PRIVATE
    "-Wl,-force_load,${AVOX_LIB_DIR}/libavox.a"
    "${AVOX_LIB_DIR}/libavcodec.a" "${AVOX_LIB_DIR}/libavformat.a"
    "${AVOX_LIB_DIR}/libavutil.a" "${AVOX_LIB_DIR}/libswresample.a"
    "${AVOX_LIB_DIR}/libzlmediakit.a" "${AVOX_LIB_DIR}/libZLToolKit.a"
    "${AVOX_LIB_DIR}/libmk_api.a"
    "${AVOX_LIB_DIR}/libflv.a" "${AVOX_LIB_DIR}/libmov.a" "${AVOX_LIB_DIR}/libmpeg.a"
    "${AVOX_LIB_DIR}/libjsoncpp.a" "${AVOX_LIB_DIR}/libfaad.a"
    "${AVOX_LIB_DIR}/libfdk-aac.a" "${AVOX_LIB_DIR}/libfreetype.a"
    "${AVOX_LIB_DIR}/libvolk.a"
    "${AVOX_WEBRTC_LIB}"
    "-Wl,-force_load,${AVOX_LIBEXT_CODEC}"
    "-framework Metal" "-framework MetalKit" "-framework UIKit" "-framework Foundation"
    "-framework CoreGraphics" "-framework CFNetwork" "-framework QuartzCore"
    "-framework CoreVideo" "-framework CoreMedia" "-framework AudioToolbox"
    "-framework AVFoundation" "-framework AVFAudio" "-framework CoreAudio"
    "-framework VideoToolbox" "-framework IOSurface" "-framework CoreFoundation"
    "-framework Security"
    z bz2 lzma iconv c++)
  set_target_properties(avoxtest PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.zhouxin.avoxtest"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${TEAM_ID}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchScreen_Generation "YES"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "avoxtest"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_NSLocalNetworkUsageDescription
      "拉取局域网测试流并回传测试日志")
else()
  # ── macOS 无头验证: 裸可执行, 离屏解码, 跑完 exit(0/1) ──
  set(AVOX_LIB_DIR "${AVOX_LIB_DIR}" CACHE PATH "目录: libavox.a/libavcodec.a/...")
  set(AVOX_WEBRTC_LIB "${AVOX_WEBRTC_LIB}" CACHE FILEPATH "libwebrtc_nosym.a (avc_library darwin 产物)")
  add_executable(avoxtest avoxtest.mm)
  avoxtest_include_dirs()
  add_library(mk_api SHARED IMPORTED)
  set_target_properties(mk_api PROPERTIES IMPORTED_LOCATION "${AVOX_LIB_DIR}/libmk_api.dylib")
  target_link_libraries(avoxtest PRIVATE
    "-Wl,-force_load,${AVOX_LIB_DIR}/libavox.a"
    "${AVOX_LIB_DIR}/libavcodec.a" "${AVOX_LIB_DIR}/libavformat.a"
    "${AVOX_LIB_DIR}/libavutil.a" "${AVOX_LIB_DIR}/libswresample.a"
    "${AVOX_LIB_DIR}/libvolk.a"
    "${AVOX_WEBRTC_LIB}"
    "-framework AudioUnit"
    mk_api
    "-framework Metal" "-framework Foundation" "-framework AppKit" "-framework AVFAudio"
    "-framework QuartzCore" "-framework CoreVideo" "-framework CoreMedia"
    "-framework AudioToolbox" "-framework AVFoundation" "-framework CoreAudio"
    "-framework VideoToolbox" "-framework IOSurface" "-framework CoreFoundation"
    "-framework CoreGraphics" "-framework Security"
    z bz2 lzma iconv c++)
  # MoltenVK: 无头回环/渲染需要, 按仓库构建产物相对路径找
  set(AVOX_MOLTENVK_LIB "${AVOX_LIB_DIR}/../MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a"
      CACHE FILEPATH "libMoltenVK.a")
  target_link_libraries(avoxtest PRIVATE "${AVOX_MOLTENVK_LIB}")
  set_target_properties(avoxtest PROPERTIES
    BUILD_RPATH "${AVOX_LIB_DIR}")
endif()

# ── 回环模式编译开关 (AVOX_MATRIX=loop 运行时需要 ffmpeg 头 + 测试源) ──
if(AVOX_FFMPEG_INCLUDE)
  target_compile_definitions(avoxtest PRIVATE AVOXTEST_HAS_FFMPEG=1)
  target_include_directories(avoxtest PRIVATE "${AVOX_FFMPEG_INCLUDE}")
endif()
