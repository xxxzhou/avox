# WIN32/UNIX/APPLE/__IPHONEOS__/__MACOSX__
set(ACV_BUILD_WASM OFF)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  set(ACV_BUILD_WASM ON)
endif()

set(KHRONOS_DIR ${AVOX_TRDPARTY}/khronos)

include_directories(${KHRONOS_DIR})
message(STATUS "khronos dir: ${KHRONOS_DIR}")

# 发行渠道 (发行包合规):
#   agpl       = AGPL 渠道, GPL 软编 libx264/libx265 可用
#   commercial = 商业(LGPL)渠道, FF 软编兜底宏按渠道实际可用编码器注入:
#                Windows = h264_mf/hevc_mf(系统自带 MFT)
#                Apple   = h264_videotoolbox/hevc_videotoolbox(FFmpeg VT wrapper, 库白名单已带)
#                Android = 原生硬编注册名(libx264/libx265 在 LGPL 包中不存在, 兜底必须落可用的编码器)
#                注意: 该渠道必须链接 LGPL 配置构建的 FFmpeg (无 --enable-gpl/libx264/libx265/nonfree),
#                打包由各发行仓 check_licenses.py 按 FFmpeg configure 串断言把关
if(AVOX_DIST_FLAVOR STREQUAL "commercial")
  message(STATUS "dist flavor: commercial (LGPL)")
  if(WIN32)
    add_compile_definitions("AVOX_FF_H264_ENCODER=\"h264_mf\"" "AVOX_FF_H265_ENCODER=\"hevc_mf\"")
  elseif(APPLE)
    add_compile_definitions("AVOX_FF_H264_ENCODER=\"h264_videotoolbox\"" "AVOX_FF_H265_ENCODER=\"hevc_videotoolbox\"")
  elseif(ANDROID)
    # Android FFmpeg LGPL 白名单无任何视频编码器, 软编兜底直接落 AndVEncoder 原生硬编(注册名与 Muxer.hpp 保持一致)
    add_compile_definitions("AVOX_FF_H264_ENCODER=\"android h264 decoder\"" "AVOX_FF_H265_ENCODER=\"android h265 decoder\"")
  endif()
else()
  message(STATUS "dist flavor: agpl (GPL 软编 libx264/libx265 可用)")
endif()

# QUIET不输出警告
# Vulka选项可用性检查 (CMAKE_MODULE_PATH中找到FindVulkan.cmake,此处为CMake自带FindVulkan.cmake)
if(AVOX_ENABLE_VULKAN)
  message(STATUS "vulkan 启用")
  # 所有平台的vulkan全在LinkVulkan.cmake中,由volk管理多平台vulkan
  include(LinkVulkan)
  include_directories(${AVOX_TRDPARTY}/volk)
  avox_update_cached_list(AVOX_LINK_LIBRARIES volk)
  add_definitions(-DAVOX_ENABLE_VULKAN)
endif()

if(NOT AVOX_ENABLE_VULKAN AND AVOX_ENABLE_VULKAN_DECODE)
  set(AVOX_ENABLE_VULKAN_DECODE OFF CACHE BOOL)
  message(WARNING "vulkan 未找到, 关闭vulkan解码")
  remove_definitions(-DAVOX_ENABLE_VULKAN)
  remove_definitions(-DAVOX_ENABLE_VULKAN_DECODE)
endif()

if(AVOX_ENABLE_VULKAN_DECODE)
  message(STATUS "vulkan decode 启用")
  add_definitions(-DAVOX_ENABLE_VULKAN_DECODE)
else()
  message(STATUS "vulkan decode 未启用")
  set(AVOX_ENABLE_VULKAN_DECODE OFF)
  remove_definitions(-DAVOX_ENABLE_VULKAN_DECODE)
endif()

# 注意find_package(Name)要与FindName.cmake里Name大小写一致
# 查找 webrtc 是否安装
if(AVOX_ENABLE_WEBRTC)
  find_package(WebRTC QUIET)
  if(WebRTC_FOUND)
    message(STATUS "webrtc includes: ${WEBRTC_INCLUDE_DIRS}")
    include_directories(${WEBRTC_INCLUDE_DIRS} ${WEBRTC_INCLUDE_DIRS}/third_party/abseil-cpp)
    add_definitions(-DAVOX_ENABLE_WEBRTC)
    # 添加了H265
    add_definitions(-DRTC_ENABLE_H265)
    # 根据不同平台添加编译符
    if(WIN32)
      add_definitions(-DWEBRTC_WIN)
    elseif(ANDROID)
      add_definitions(-DWEBRTC_POSIX)
      add_definitions(-DWEBRTC_ANDROID)
      # 关闭 WebRTC 内部调试检查，避免链接 Release 版 webrtc 库时找不到 ExpectationToString 符号
      add_definitions(-DNDEBUG)
    elseif(APPLE)
      add_definitions(-DWEBRTC_POSIX)
      # IOS也需要这个宏,没有会导致大小端编译符找不到
      add_definitions(-DWEBRTC_MAC)
      add_definitions(-DWEBRTC_IOS)
      # 关闭 WebRTC 内部所有调试路径
      add_definitions(-DNDEBUG)
      message(STATUS "webrtc mac 编译符: ${CMAKE_CXX_FLAGS}")
    endif()
    # webrtc.lib 已迁 plugins/avox_webrtc, 不再链入 avox.dll
    # 静态链接时仍需链入 avox(由 plugins/avox_webrtc/CMakeLists.txt 处理)
    # Win32 平台库(winmm等)也移到插件链接
  else()
    # 虽然定义了，但是找不到模块，需要关闭AVOX_ENABLE_WEBRTC
    set(AVOX_ENABLE_WEBRTC OFF)
    message(WARNING "webrtc 未找到")
    remove_definitions(-DAVOX_ENABLE_WEBRTC)
  endif()
endif()

# 查找 ffmpeg 是否安装
if(AVOX_ENABLE_FFMPEG)
  find_package(FFmpeg)
  if(FFMPEG_FOUND)
    message(STATUS "ffmpeg library: ${FFMPEG_LIBRARIES}")
    message(STATUS "ffmpeg include: ${FFMPEG_INCLUDE_DIRS}")
    include_directories(${FFMPEG_INCLUDE_DIRS})
    avox_update_cached_list(AVOX_LINK_LIBRARIES ${FFMPEG_LIBRARIES})
    add_definitions(-DAVOX_ENABLE_FFMPEG)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_FFMPEG OFF)
    message(WARNING "ffmpeg 未找到")
    remove_definitions(-DAVOX_ENABLE_FFMPEG)
  endif()
endif()

# 查找 libtorrent(avox_torrent 插件依赖: 头文件+静态库, 预编译产物在库仓)
# 源项目独立维护: D:/Work/github/libtorrent 下 python build_windows.py 编译安装
if(AVOX_ENABLE_TORRENT)
  find_package(Libtorrent QUIET)
  if(Libtorrent_FOUND)
    include_directories(${LIBTORRENT_INCLUDE_DIRS} ${Libtorrent_BOOST_DIR})
  else()
    set(AVOX_ENABLE_TORRENT OFF)
    message(STATUS "libtorrent 未找到, 关闭 avox_torrent(见 D:/Work/github/libtorrent)")
  endif()
endif()

# OpenSSL / BoringSSL 统一查找
# - Agent(httplib): WebRTC 启用且找到时优先复用其自带的 BoringSSL，否则回退 OpenSSL 3.0.0+
#   复用 BoringSSL 可避免 avox.dll 内 BoringSSL(webrtc 静态链入) 与 OpenSSL(动态链入) 的 SSL_* 符号撞车
# - ZLMediaKit: 始终用 OpenSSL (mk_api.dll 独立链接, 不影响 avox.dll)
set(AVOX_OPENSSL_MIN_VERSION "3.0.0" CACHE STRING "Minimum OpenSSL version required by Agent/cpp-httplib")

# 决定 Agent 是否复用 WebRTC 的 BoringSSL
# 静态链接(AVOX_DLL_TYPE=STATIC): webrtc 仍在同一二进制, BoringSSL 可用 → 复用
# 动态链接(SHARED): webrtc.lib 移入 avox_webrtc.dll, BoringSSL 不在 avox.dll → 用 OpenSSL 3.0+
set(AVOX_AGENT_USE_BORINGSSL OFF)
if(AVOX_ENABLE_AGENT AND AVOX_ENABLE_WEBRTC AND WebRTC_FOUND)
  if(AVOX_DLL_TYPE STREQUAL "STATIC")
    # 静态链接: webrtc 和 avox 同一二进制, BoringSSL 仍可用
    if(WEBRTC_BORINGSSL_INCLUDE_DIR AND EXISTS "${WEBRTC_BORINGSSL_INCLUDE_DIR}/openssl/ssl.h")
      set(AVOX_AGENT_USE_BORINGSSL ON)
      message(STATUS "Agent: 静态链接, 复用 WebRTC BoringSSL")
    endif()
  else()
    # 动态链接: BoringSSL 在 avox_webrtc.dll, avox.dll 不可引用 → 必须用 OpenSSL
    message(STATUS "Agent: 动态链接, webrtc 在插件, BoringSSL 不可用, 需要 OpenSSL")
  endif()
endif()

# OpenSSL 查找: ZLMediaKit 需要; Agent 在未用 BoringSSL 时也需要
if(AVOX_ENABLE_ZLMEDIAKIT OR (AVOX_ENABLE_AGENT AND NOT AVOX_AGENT_USE_BORINGSSL))
  # Agent 用 OpenSSL 时强制 3.0.0+; 否则(仅 ZLMediaKit)不强制版本
  if(AVOX_ENABLE_AGENT AND NOT AVOX_AGENT_USE_BORINGSSL)
    find_package(OpenSSL ${AVOX_OPENSSL_MIN_VERSION} QUIET)
  else()
    find_package(OpenSSL QUIET)
  endif()
  if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_LIBRARIES}")
    # 定义全局 OpenSSL 已找到标志，供 FindZLMediaKit.cmake 复用
    set(AVOX_OPENSSL_FOUND ON CACHE INTERNAL "OpenSSL found by AVOXOptions")
    # Windows: 复制 OpenSSL DLL 到运行目录 (mk_api.dll 运行时需要)
    if(WIN32 AND OPENSSL_DLLS)
      foreach(_openssl_dll ${OPENSSL_DLLS})
        avox_run_module_copy("${_openssl_dll}")
      endforeach()
    endif()
    # 仅当 Agent 使用 OpenSSL 时, 才把 OpenSSL 链进 avox.dll 并加全局 include。
    # Agent 用 BoringSSL 时, avox.dll 不需要 OpenSSL (其头/库由 BoringSSL 在 src/CMakeLists.txt 按文件作用域提供)。
    if(AVOX_ENABLE_AGENT AND NOT AVOX_AGENT_USE_BORINGSSL)
      include_directories(${OPENSSL_INCLUDE_DIRS})
      avox_update_cached_list(AVOX_LINK_LIBRARIES ${OPENSSL_LIBRARIES})
    endif()
  elseif(AVOX_ENABLE_AGENT AND NOT AVOX_AGENT_USE_BORINGSSL)
    # Agent 需要 OpenSSL 3.0.0+ 但未找到
    set(AVOX_ENABLE_AGENT OFF)
    message(WARNING "OpenSSL ${AVOX_OPENSSL_MIN_VERSION}+ 未找到，Agent 模块已关闭 (可设置 OPENSSL_ROOT_DIR 指定路径)")
    remove_definitions(-DAVOX_ENABLE_AGENT)
  elseif(AVOX_ENABLE_ZLMEDIAKIT)
    # ZLMediaKit 可以没有 OpenSSL（SSL 功能禁用）
    message(WARNING "OpenSSL 未找到，ZLMediaKit SSL 功能将禁用")
  endif()
endif()

if(AVOX_ENABLE_ZLMEDIAKIT)
  # 注意大小写要一致，这个名字要与对应的FindZLMediaKit.cmake文件一致
  find_package(ZLMediaKit)
  if(ZLMediaKit_FOUND)
    message(STATUS "zlmediakit library: ${ZLMEDIAKIT_LIBRARIES}")
    message(STATUS "zlmediakit include: ${ZLMEDIAKIT_INCLUDE_DIRS}")
    include_directories(${ZLMEDIAKIT_INCLUDE_DIRS})
    avox_update_cached_list(AVOX_LINK_LIBRARIES ${ZLMEDIAKIT_LIBRARIES})
    add_definitions(-DAVOX_ENABLE_ZLMEDIAKIT)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_ZLMEDIAKIT OFF)
    message(WARNING "ZLMediaKit 未找到")
    remove_definitions(-DAVOX_ENABLE_ZLMEDIAKIT)
  endif()
endif()

# 只有WIN32平台才支持faac
# 查找 faac 是否安装
if(AVOX_ENABLE_FAAC)
  find_package(Faac QUIET)
  if(FAAC_FOUND)
    message(STATUS "faac library: ${FAAC_LIBRARYS}")
    include_directories(${FAAC_INCLUDE_DIRS})
    avox_update_cached_list(AVOX_LINK_LIBRARIES ${FAAC_LIBRARYS})
    add_definitions(-DAVOX_ENABLE_FAAC)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_FAAC OFF)
    message(WARNING "faac 未找到")
    remove_definitions(-DAVOX_ENABLE_FAAC)
  endif() 
endif()

# 查找 fdk-aac 是否安装
if(AVOX_ENABLE_FDKAAC)
  find_package(Fdkaac QUIET)
  if(FDKAAC_FOUND)
    message(STATUS "fdk-aac library: ${FDKAAC_LIBRARYS}")
    include_directories(${FDKAAC_INCLUDE_DIRS})
    avox_update_cached_list(AVOX_LINK_LIBRARIES ${FDKAAC_LIBRARYS})
    add_definitions(-DAVOX_ENABLE_FDKAAC)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_FDKAAC OFF)
    message(WARNING "fdk-aac 未找到")
    remove_definitions(-DAVOX_ENABLE_FDKAAC)
  endif()
endif()

# 查找 freetype 是否安装
if(AVOX_ENABLE_FREETYPE)
  find_package(Freetype QUIET)
  # 如果是linux,还需要PNG、BZ2和Brotli库
  if(ONLY_LINUX)
    find_package(PNG QUIET)
    if(NOT PNG_FOUND)
      message(WARNING "freetype need png,png not found,install png")      
      set(FREETYPE_FOUND OFF)
    endif()
    find_package(BZip2 QUIET)
    if(NOT BZip2_FOUND)
      message(WARNING "freetype need bz2,bz2 not found,install bz2")
      set(FREETYPE_FOUND OFF)
    endif()
  endif()
  if(FREETYPE_FOUND)
    message(STATUS "freetype library: ${FREETYPE_LIBRARYS}")
    include_directories(${FREETYPE_INCLUDE_DIRS})
    avox_update_cached_list(AVOX_LINK_LIBRARIES ${FREETYPE_LIBRARYS})
    if(ONLY_LINUX)
      avox_update_cached_list(AVOX_LINK_LIBRARIES ${PNG_LIBRARIES} ${BZIP2_LIBRARIES} ${Brotli_LIBRARIES})
    endif()    
    add_definitions(-DAVOX_ENABLE_FREETYPE)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_FREETYPE OFF)
    message(WARNING "freetype 未找到")
    remove_definitions(-DAVOX_ENABLE_FREETYPE)
  endif()
endif()

# ONNX Runtime 推理引擎 (AI 模块共用)
if(AVOX_ENABLE_ONNX)
  find_package(ONNX QUIET)
  if(ONNX_FOUND)
    message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIBRARIES}")
    include_directories(${ONNXRUNTIME_INCLUDE_DIRS})
    add_definitions(-DAVOX_ENABLE_ONNX)
    # onnxruntime 不再链入 avox(已迁 plugins/avox_onnx); 仅保留 find_package/include/宏 供 plugin 子目录继承
  else()
    set(AVOX_ENABLE_ONNX OFF)
    message(WARNING "ONNX Runtime 未找到，关闭 ONNX 支持")
    remove_definitions(-DAVOX_ENABLE_ONNX)
  endif()
endif()

# OpenVINO 推理引擎 (VkQEnhanceLayer 画质增强; Intel iGPU/CPU, 运行期降级 ORT)
if(AVOX_ENABLE_OPENVINO)
  find_package(OpenVINO QUIET)
  if(OpenVINO_FOUND)
    message(STATUS "OpenVINO Runtime found: ${OPENVINO_LIBRARIES}")
    include_directories(${OPENVINO_INCLUDE_DIRS})
    add_definitions(-DAVOX_ENABLE_OPENVINO)
    # openvino 不链入 avox(在 plugins/avox_openvino); 仅 find_package/include/宏 供 plugin 继承
  else()
    set(AVOX_ENABLE_OPENVINO OFF)
    message(WARNING "OpenVINO 未找到, 关闭 OpenVINO 支持 (VkQEnhanceLayer 将走 ORT CPU)。提取: python script/openvino/extract_openvino.py")
    remove_definitions(-DAVOX_ENABLE_OPENVINO)
  endif()
endif()

# sherpa-onnx可用（流式语音识别）
if(AVOX_ENABLE_SHERPA)
  find_package(SherpaOnnx QUIET)
  if(SherpaOnnx_FOUND)
    message(STATUS "sherpa-onnx library: ${SHERPA_LIBRARYS}")
    include_directories(${SHERPA_INCLUDE_DIRS})
    # sherpa-onnx 不再链入 avox(已迁 plugins/avox_sherpa); 仅保留 find_package/include/宏 供 plugin 子目录继承
    add_definitions(-DAVOX_ENABLE_SHERPA)
  else()
    # 虽然定义了，但是找不到模块，需要关闭
    set(AVOX_ENABLE_SHERPA OFF)
    message(WARNING "sherpa-onnx 未找到")
    remove_definitions(-DAVOX_ENABLE_SHERPA)
  endif()
endif()

# 需要同时有onnx和sentencepiece才启用
if(AVOX_ENABLE_TRANSLATION)
  message(STATUS "translation 启用")
  # 先查找 sentencepiece
  find_package(SentencePiece QUIET)
  if(NOT SentencePiece_FOUND)
    set(AVOX_ENABLE_TRANSLATION OFF)
    message(WARNING "sentencepiece 未找到")
  endif()
  if(ONNX_FOUND AND SentencePiece_FOUND)
    include_directories(${SPM_INCLUDE_DIRS})
    add_definitions(-DAVOX_ENABLE_TRANSLATION)
    # sentencepiece 不再链入 avox(已迁 plugins/avox_translation); 仅保留 find_package/include/宏 供 plugin 继承
    message(STATUS "语言翻译 onnx + sentencepiece 已就绪")
  else()
    set(AVOX_ENABLE_TRANSLATION OFF)
    if(NOT ONNX_FOUND)
      message(WARNING "onnx 未找到")
    endif()
    if(NOT SentencePiece_FOUND)
      message(WARNING "sentencepiece 未找到")
    endif()
    remove_definitions(-DAVOX_ENABLE_TRANSLATION)
  endif()
endif()

# Agent 模块配置（cpp-httplib）
# SSL 来源: 优先 WebRTC 自带 BoringSSL, 否则 OpenSSL (二者由上方统一决策)
if(AVOX_ENABLE_AGENT)
  if(AVOX_AGENT_USE_BORINGSSL)
    include_directories(${AVOX_TRDPARTY}/cpp-httplib)
    add_definitions(-DAVOX_ENABLE_AGENT)
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
    message(STATUS "Agent module enabled with cpp-httplib + BoringSSL (WebRTC)")
  elseif(OpenSSL_FOUND)
    include_directories(${AVOX_TRDPARTY}/cpp-httplib)
    add_definitions(-DAVOX_ENABLE_AGENT)
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
    message(STATUS "Agent module enabled with cpp-httplib + OpenSSL")
  else()
    set(AVOX_ENABLE_AGENT OFF)
    message(WARNING "Agent 模块需要 OpenSSL ${AVOX_OPENSSL_MIN_VERSION}+ 或 WebRTC(BoringSSL)，已关闭")
  endif()
endif()

# OpenCV 可选（用于图像处理、形态学操作等）
# 核心不再链 opencv —— cv 能力(load/resize/匹配/掩码…)全在 plugins/avox_opencv, 核心经
# imageProcHub 等运行期 hub 调用(无编译期 cv 符号, 与 onnx 同模式)。此处仅 find_package +
# include + 宏 供插件子目录继承; opencv_world4xx.dll 只随 avox_opencv 进 plugins/。
if(AVOX_ENABLE_OPENCV)
  find_package(OpenCV QUIET)
  if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
    include_directories(${OpenCV_INCLUDE_DIRS})
    add_definitions(-DAVOX_ENABLE_OPENCV)
  else()
    set(AVOX_ENABLE_OPENCV OFF)
    message(WARNING "OpenCV 未找到，关闭 OpenCV 支持")
    remove_definitions(-DAVOX_ENABLE_OPENCV)
  endif()
endif()

# AVOX_ENABLE_CV 的依赖检查(onnx + opencv)已下放给 plugins/avox_cv/CMakeLists.txt 的
# find_package 处理(找不到库自动 return 跳过), 此处不再 gate, 也不再 add_definitions。
# 核心纯接口 IWatermarkRemoval 在 src/avox/inpaint/(随 avox.dll 编译, 无 onnx/opencv 依赖)。

# 如果是WINDOWS平台，刚需DX11/DX12
if(WIN32)
  find_package(DirectX11 REQUIRED)
  avox_update_cached_list(D3D12_LIBRARIES d3d12.lib)
  message(STATUS "dx11 include:" ${DirectX11_INCLUDE_DIRS})
  message(STATUS "dx12 include:" ${D3D12_INCLUDE_DIRS})
  message(STATUS "dx11 libs:" ${DirectX11_LIBRARY})
  message(STATUS "dx12 libs:" ${D3D12_LIBRARIES})
  # WinRT Graphics Capture 需要 windowsapp.lib
  avox_update_cached_list(AVOX_LINK_LIBRARIES ${DirectX11_LIBRARY} ${D3D12_LIBRARIES} windowsapp.lib)
endif()

if(ANDROID)
  include_directories(${ANDROID_NDK}/sources/android/native_app_glue/include ${ANDROID_NDK}/sources/android/native_app_glue)
  message(STATUS "android native_app_glue include: ${ANDROID_NDK}/sources/android/native_app_glue")
  add_library(native-app-glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
  avox_update_cached_list(AVOX_LINK_LIBRARIES c++_shared c++abi native-app-glue log android EGL GLESv2 GLESv3 OpenSLES jnigraphics mediandk camera2ndk)
  if(AVOX_ENABLE_FREETYPE)
    # 静态链接freetype需要加上zlib
    avox_update_cached_list(AVOX_LINK_LIBRARIES z)
  endif()
endif()

if(ONLY_LINUX)
  find_package(PkgConfig REQUIRED)
  find_package(X11 REQUIRED)
  find_package(ZLIB REQUIRED)

  # 添加包含路径
  include_directories(${X11_INCLUDE_DIRS})
  message(STATUS "X11 include:" ${X11_INCLUDE_DIRS})
  message(STATUS "X11 libs:" ${X11_LIBRARIES})

  # 添加链接库
  avox_update_cached_list(AVOX_LINK_LIBRARIES ${X11_LIBRARIES} ${ZLIB_LIBRARIES})
  add_definitions(-D__ONLY_LINUX__)
  add_definitions(-DAVOX_ENABLE_X11)
endif()

if(IOS)
  # 动态获取 iOS SDK 路径
  execute_process(
    COMMAND xcrun --sdk iphoneos --show-sdk-path
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # 为 iOS 真机设置库路径
  set(SYSTEM_LIBRARY_SEARCH_PATHS
    "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks"
    "${CMAKE_OSX_SYSROOT}/usr/lib")
  link_directories(${SYSTEM_LIBRARY_SEARCH_PATHS})
  message(STATUS "Set iOS system library search paths: ${SYSTEM_LIBRARY_SEARCH_PATHS}")
endif()

# avox_apple模块需要的框架列表(Apple 平台通用; Metal/VideoToolbox/AVFoundation 相关代码无条件编译, 框架不随功能开关)
if(APPLE)
  if(IOS)
    set(COMMON_FRAMEWORKS Foundation UIKit GLKit OpenGLES IOSurface Metal QuartzCore)
  else()
    # macOS: iOS 独有(UIKit/GLKit/OpenGLES)换 AppKit, avox_apple 依赖的媒体框架全列
    set(COMMON_FRAMEWORKS Foundation AppKit QuartzCore Metal IOSurface CoreGraphics CoreMedia CoreVideo VideoToolbox AudioToolbox AudioUnit CoreAudio AVFoundation Security CoreFoundation CFNetwork)
  endif()
  if(AVOX_ENABLE_FFMPEG)
    avox_list_append_unique(COMMON_FRAMEWORKS AVFoundation CoreGraphics CoreMedia VideoToolbox AudioToolbox CoreVideo z bz2 iconv)
  endif()
  if(AVOX_ENABLE_ZLMEDIAKIT)
    avox_list_append_unique(COMMON_FRAMEWORKS Security CoreFoundation CFNetwork)
  endif()
  if(AVOX_ENABLE_WEBRTC)
    avox_list_append_unique(COMMON_FRAMEWORKS
      Security
      CoreFoundation
      CFNetwork
      AVFoundation
      CoreMedia
      CoreVideo
      AudioToolbox
      VideoToolbox
      CoreGraphics)
    # GLKit/OpenGLES 是 iOS 独有框架, macOS 上不存在(find_library 失败即 FATAL)
    if(IOS)
      avox_list_append_unique(COMMON_FRAMEWORKS GLKit OpenGLES)
    endif()
  endif()

  # if(AVOX_ENABLE_FDKAAC)
  #   avox_list_append_unique(COMMON_FRAMEWORKS fdk-aac)
  # endif()

  # if(AVOX_ENABLE_FREETYPE)
  #   avox_list_append_unique(COMMON_FRAMEWORKS freetype)
  # endif()

  set(MISSING_FRAMEWORKS "")

  foreach(FRAMEWORK_NAME IN LISTS COMMON_FRAMEWORKS)
    string(TOUPPER ${FRAMEWORK_NAME} FRAMEWORK_VAR_NAME)
    string(APPEND FRAMEWORK_VAR_NAME "_FRAMEWORK")
    # 查找框架
    find_library(${FRAMEWORK_VAR_NAME} ${FRAMEWORK_NAME})
    if(${FRAMEWORK_VAR_NAME})
      message(STATUS "Found ${FRAMEWORK_NAME}.framework: ${${FRAMEWORK_VAR_NAME}}")
      # 将框架添加到链接库列表
      avox_update_cached_list(AVOX_LINK_LIBRARIES ${${FRAMEWORK_VAR_NAME}})
    else()
      list(APPEND MISSING_FRAMEWORKS ${FRAMEWORK_NAME})
    endif()
  endforeach()

  # 处理缺失的框架
  if(MISSING_FRAMEWORKS)
    string(REPLACE ";" ", " MISSING_FRAMEWORKS_STR "${MISSING_FRAMEWORKS}")
    message(FATAL_ERROR "The following frameworks are not found: ${MISSING_FRAMEWORKS_STR}")
  endif()
endif()

if(AVOX_ENABLE_SWIG)
  find_package(SWIG QUIET)
  if(SWIG_FOUND AND AVOX_ENABLE_SWIG)
    add_definitions(-DAVOX_ENABLE_SWIG)
    message(STATUS "swig library: ${SWIG_LIBRARIES}")
  else()
    set(AVOX_ENABLE_SWIG OFF)
    message(WARNING "swig 没找到")
  endif()
else()
  message(WARNING "swig 不启用")
endif()