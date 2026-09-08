# 需要指定WebRTC源码位置，从源码位置编译出webrtc库。
# 头文件与库都直接根据webrtc源码目录来设定，
# 不想把webrtc源码的头文件复制到当前项目中，因为太多文件了，其lib也很大。

# WebRTC 依赖目录结构:
#   src/                  # 头文件 (api/, rtc_base/, modules/, media/, sdk/)
#   build/
#     windows/release/    # webrtc_nosym.lib (无符号,入库) / webrtc.lib (带符号,不入库)
#     windows/debug/      # webrtc.lib (需本机自编, 不入库)
#     android/release/    # libwebrtc_nosym.a / libwebrtc.a
#     ios/release/        # libwebrtc_nosym.a / libwebrtc.a
#     darwin/release/     # libwebrtc_nosym.a (macOS)
# 优先链接无符号_nosym版(avc_library入库的), 找不到时回退带符号版(本机自编)

# 确定 WebRTC 根目录
# 优先使用 AVOX_EXTERNAL_LIBRARY_DIR (在根 CMakeLists.txt 中定义)
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    set(WEBRTC_BASE_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}")
else()
    # 向后兼容：未定义时计算默认值
    get_filename_component(AVOX_PARENT_DIR ${CMAKE_SOURCE_DIR}/.. REALPATH)
    set(WEBRTC_BASE_DIR "${AVOX_PARENT_DIR}/avc_library")
endif()

# 设置头文件和库目录
set(WEBRTC_SOURCE_DIR "${WEBRTC_BASE_DIR}/src")
# WebRTC 库统一放在 release 目录，不区分 Debug/Release
# 使用小写的系统名称（与实际目录结构匹配）
string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LOWER)
set(WEBRTC_BUILD_DIR "${WEBRTC_BASE_DIR}/build/${CMAKE_SYSTEM_NAME_LOWER}/release")
if(WIN32 AND AVOX_DEBUG)
    set(WEBRTC_BUILD_DIR "${WEBRTC_BASE_DIR}/build/${CMAKE_SYSTEM_NAME_LOWER}/debug")
endif()

message(STATUS "WEBRTC_BASE_DIR: ${WEBRTC_BASE_DIR}")
message(STATUS "WEBRTC_SOURCE_DIR: ${WEBRTC_SOURCE_DIR}")
message(STATUS "WEBRTC_BUILD_DIR: ${WEBRTC_BUILD_DIR}")

# 查找WEBRTC的头文件目录
find_path(WEBRTC_INCLUDE_DIR
    NAMES api/peer_connection_interface.h
    PATHS ${WEBRTC_SOURCE_DIR}
    NO_DEFAULT_PATH)
set(WEBRTC_INCLUDE_DIRS ${WEBRTC_INCLUDE_DIR})

# WebRTC 依赖 abseil，头文件在 avc_library/src/third_party/abseil-cpp
set(ABSL_INCLUDE_DIR "${WEBRTC_SOURCE_DIR}/third_party/abseil-cpp")
if(EXISTS ${ABSL_INCLUDE_DIR})
    list(APPEND WEBRTC_INCLUDE_DIRS ${ABSL_INCLUDE_DIR})
    message(STATUS "ABSL_INCLUDE_DIR: ${ABSL_INCLUDE_DIR}")
endif()

# libyuv (Windows NV12→I420 conversion)
set(LIBYUV_INCLUDE_DIR "${WEBRTC_SOURCE_DIR}/third_party/libyuv/include")
if(EXISTS ${LIBYUV_INCLUDE_DIR})
    list(APPEND WEBRTC_INCLUDE_DIRS ${LIBYUV_INCLUDE_DIR})
    message(STATUS "LIBYUV_INCLUDE_DIR: ${LIBYUV_INCLUDE_DIR}")
endif()

# BoringSSL 头文件 (WebRTC 自带的 SSL 库)
# 供需要 SSL 的模块复用 (如 Agent/cpp-httplib): 复用同一份 BoringSSL 可避免
# avox.dll 内 BoringSSL(webrtc 静态链入) 与 OpenSSL(动态链入) 的 SSL_* 符号撞车。
# 只需头文件, 实际符号由 webrtc 库提供。
set(WEBRTC_BORINGSSL_INCLUDE_DIR "${WEBRTC_SOURCE_DIR}/third_party/boringssl/src/include")
if(EXISTS "${WEBRTC_BORINGSSL_INCLUDE_DIR}/openssl/ssl.h")
    message(STATUS "WEBRTC_BORINGSSL_INCLUDE_DIR: ${WEBRTC_BORINGSSL_INCLUDE_DIR}")
else()
    set(WEBRTC_BORINGSSL_INCLUDE_DIR "")
    message(STATUS "WebRTC BoringSSL 头未找到 (仅 WebRTC 自用 SSL 时可忽略)")
endif()

# 查找WEBRTC库 (nosym无符号版优先, 回退带符号版)
if(WIN32)
    if(AVOX_DEBUG)
        find_library(WEBRTC_LIBRARY
            NAMES webrtc webrtc_nosym
            PATHS ${WEBRTC_BUILD_DIR}
            NO_DEFAULT_PATH)
    else()
        find_library(WEBRTC_LIBRARY
            NAMES webrtc_nosym webrtc
            PATHS ${WEBRTC_BUILD_DIR}
            NO_DEFAULT_PATH)
    endif()
elseif(APPLE)
    find_library(WEBRTC_LIBRARY
        NAMES webrtc_nosym webrtc
        PATHS ${WEBRTC_BUILD_DIR}
        NO_DEFAULT_PATH)
elseif(ANDROID)
    find_library(WEBRTC_LIBRARY
        NAMES webrtc_nosym webrtc
        PATHS ${WEBRTC_BUILD_DIR}
        NO_DEFAULT_PATH)
endif()

set(WEBRTC_LIBRARIES ${WEBRTC_LIBRARY})

message(STATUS "WEBRTC_INCLUDE_DIRS: ${WEBRTC_INCLUDE_DIRS}")
message(STATUS "WEBRTC_LIBRARIES: ${WEBRTC_LIBRARIES}")

# WEBRTC_FOUND变量
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WebRTC DEFAULT_MSG WEBRTC_LIBRARIES WEBRTC_INCLUDE_DIRS)