# 将版本号更新到 3.10 或更高
cmake_minimum_required(VERSION 3.10)

# 简化CMAKE的语法
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS TRUE)
# 生成compile_commands.json文件，用于IDE的代码补全
# set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# 不使用 vcpkg
if(WIN32)
    set(CMAKE_TOOLCHAIN_FILE OFF)
endif()

# 项目名称
project(avplay)

if(APPLE)
    enable_language(OBJCXX)
endif()

# C++17支持,但是在linux编译webrtc模块需要20支持
set(CMAKE_CXX_STANDARD 20)

# IOS平台相关设置
# 关闭代码签名
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")  
# 动态库在IOS下需要签名,根据配置自己更改
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development: xxxxxx@163.com (F55URGYB7K)")
# 添加 CODE_SIGNING_ALLOWED=NO 配置
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")

# 指定授权文件 授权文件定义了应用可以使用的特定功能和权限，像推送通知、应用内购买等。
# set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/Entitlements.plist")

# 上面的CMAKE_CXX_STANDARD可能没起作用
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17")
set(CMAKE_XCODE_SCHEME_ENABLE_GPU_API_VALIDATION FALSE)
set(CMAKE_XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE DISABLED)

# cmake查找模块添加自定义的路径
# cmake本身也会提供一些FileModule.cmake,一般在安装目录下的Modules目录下
# 例如: C:\Program Files\CMake\share\cmake-3.30\Modules
# 提供常见的有CUDA/Freetype/GLU/GLUT/GLEW
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

# 外部依赖库根目录 (webrtc, onnxruntime, opencv 等大型库)
# 优先级: 1. CMake 命令行参数 (-DAVOX_EXTERNAL_LIBRARY_DIR=xxx)
#        2. 环境变量 AVOX_EXTERNAL_LIBRARY_DIR
#        3. 默认 ../avc_library (与 avplay 同级目录)
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    # 使用命令行传入的值
    get_filename_component(AVOX_EXTERNAL_LIBRARY_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}" REALPATH)
elseif(DEFINED ENV{AVOX_EXTERNAL_LIBRARY_DIR})
    # 使用环境变量
    get_filename_component(AVOX_EXTERNAL_LIBRARY_DIR "$ENV{AVOX_EXTERNAL_LIBRARY_DIR}" REALPATH)
else()
    # 默认: 与 avplay 同级的 avc_library 目录
    get_filename_component(AVOX_EXTERNAL_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/.." REALPATH)
    set(AVOX_EXTERNAL_LIBRARY_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/avc_library")
endif()
message(STATUS "AVOX_EXTERNAL_LIBRARY_DIR: ${AVOX_EXTERNAL_LIBRARY_DIR}")

# 检查编译类型
set(AVOX_DEBUG 0)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(AVOX_DEBUG 1)
endif()
string(TOLOWER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_LOWER)
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_LOWER: ${CMAKE_BUILD_TYPE_LOWER}")

message(STATUS "AVOX_DEBUG: ${AVOX_DEBUG}")
add_definitions(-DAVOX_DEBUG=${AVOX_DEBUG})

# 只是LINUX平台，不包含ANDROID与IOS
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT APPLE)
    set(ONLY_LINUX TRUE)
endif()
# 生成目录(cmake指定生成目录如下结构，因为是顶级目录，需要在上层指定)
# set(CMAKE_BINARY_DIR "${CMAKE_SOURCE_DIR}/build/${CMAKE_SYSTEM_NAME}/${PROJECT_NAME}")
message(STATUS "Build Directory: ${CMAKE_BINARY_DIR}")
# 大小写敏感,Cmake里是Linux,但是生成目录是linux
string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LOWER)
if(ONLY_LINUX OR ANDROID)   
    set(AVOX_MOEDULE_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/${CMAKE_SYSTEM_NAME_LOWER}")
else()
    set(AVOX_MOEDULE_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/${CMAKE_SYSTEM_NAME}")
endif()
message(STATUS "AVOX_MOEDULE_BUILD_DIR: ${AVOX_MOEDULE_BUILD_DIR}")
# 检查64/32位
include(CheckTypeSize)
# cmake_dependent_option
include(CMakeDependentOption)
# 使用build_android.py会自动包含，但是Android studio下不行
# Android studio新版本需要指定ANDROID_NDK，自动生成的路径带\
# 需要在build.gradle里手动指定不带\的路径
if(ANDROID)
    include(${ANDROID_NDK}/build/cmake/android.toolchain.cmake)
    message(STATUS "ANDROID_NDK:${ANDROID_NDK_HOME}")
endif()
check_type_size("void*" AVOX_PTR_SIZE BUILTIN_TYPES_ONLY)
if(AVOX_PTR_SIZE EQUAL 8)
    set(AVOX_PLATFORM_X64 TRUE)
    add_definitions(-DAVOX_PLATFORM_X64)
else()
    set(AVOX_PLATFORM_X64 FALSE)
    remove_definitions(-DAVOX_PLATFORM_X64)
endif()
message(STATUS "Is64: ${AVOX_PLATFORM_X64}")
# CMake构建系统信息
message(STATUS "CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME}")
# 运行系统类似，构建可能是windows,运行可能是android
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
# 系统版本
message(STATUS "CMAKE_SYSTEM_VERSION: ${CMAKE_SYSTEM_VERSION}")
# 框架AMD64/x86/armv7a/arm64-v8a
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
# 安装路径,生成目录类似(install/AMD64)|(install/armv7a)|(install/aarch64)
# ${CMAKE_SYSTEM_NAME}在build前确定，在CMAKE_BINARY_DIR包含，故去掉${CMAKE_SYSTEM_NAME}
# windows/apple其库生成会在CMAKE_INSTALL_PREFIX自动带CMAKE_BUILD_TYPE
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install/${CMAKE_SYSTEM_PROCESSOR}" CACHE PATH "default install path" FORCE)
set(CMAKE_AVALONIA_LIBS "${CMAKE_SOURCE_DIR}/platform/avalonia/DDPlayer/AvaPlayer/AvaPlayer.Android/Libs" )

if(ANDROID)
    message(STATUS "Using ANDROID_ABI: ${ANDROID_ABI}")
    message(STATUS "Using ANDROID_PLATFORM: ${ANDROID_PLATFORM}")
endif()

# 如果是windows下,使用Clang,CMAKE_INSTALL_PREFIX后面不会自动带CMAKE_BUILD_TYPE
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}" CACHE PATH "default install path" FORCE)
endif()

set(CMAKE_INSTALL_LIB ${CMAKE_INSTALL_PREFIX})
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# 新增交叉编译模式配置
if(NOT "${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "${CMAKE_SYSTEM_NAME}")
    message(STATUS "检测到交叉编译环境，调整查找模式")
    if(ANDROID OR IOS)
        # Clang 编译器默认不识别 MSVC 的 __declspec 关键字，需要添加 -fms-extensions 选项来启用它。
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fms-extensions")
        set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
        set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
        set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
    endif()
endif()

if(ANDROID)
    message(STATUS "find android ndk: ${ANDROID_NDK}")
    # -std=c++14
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")
endif()

if(IOS) 
  # Add iOS-specific compiler flags to suppress WebRTC nullability warnings
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-nullability-completeness -Wno-nullability-extension")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nullability-completeness -Wno-nullability-extension")
endif()

# 封装一些常用的方法AVOX_update_cached/AVOX_update_cached_list
include(AVOXHelper)

# 链接库
avox_update_cached_list(AVOX_LINK_LIBRARIES "")
set(AVOX_TRDPARTY ${CMAKE_SOURCE_DIR}/3rdparty)
if(UNIX) 
    set(AVOX_TRDPARTY_LIBRAY ${AVOX_TRDPARTY}/library/${CMAKE_SYSTEM_NAME_LOWER})
else()
    set(AVOX_TRDPARTY_LIBRAY ${AVOX_TRDPARTY}/library/${CMAKE_SYSTEM_NAME})
endif()
message(STATUS "AVOX_TRDPARTY_LIBRAY: ${AVOX_TRDPARTY_LIBRAY}")
if(UNIX) 
    set(AVOX_PLATFORM_PATH ${CMAKE_SOURCE_DIR}/platform/${CMAKE_SYSTEM_NAME_LOWER})
else()
    set(AVOX_PLATFORM_PATH ${CMAKE_SOURCE_DIR}/platform/${CMAKE_SYSTEM_NAME})
endif()
message(STATUS "AVOX_PLATFORM_PATH: ${AVOX_PLATFORM_PATH}")

# 第三方库(第三方库查找结果影响AVOXOptions,需要放到AVOXOptions前)
# add_subdirectory(3rdparty)

# 系统分别配置(WIN32, APPLE, UNIX)
include(AVOXPlatform)

# 生成版本号
include(AVOXGit)

# Option设置(根据选项配置查找第三方库,确定是否可用)
option(AVOX_ENABLE_SWIG "build swig" ON)
option(AVOX_ENABLE_SWIG_CSHARP "build swig csharp" ON)
option(AVOX_ENABLE_SWIG_JAVA "build swig java" ON)
option(AVOX_ENABLE_SWIG_NODEJS "build swig node js" ON)
option(AVOX_ENABLE_VULKAN "build vulkan" ON)
option(AVOX_ENABLE_VULKAN_DECODE "build vulkan decode" OFF)
option(AVOX_ENABLE_ZLMEDIAKIT "build zlmediakit" ON)
option(AVOX_ENABLE_WEBRTC "build webrtc" ON)
option(AVOX_ENABLE_FAAD2 "build faad2" ON)
option(AVOX_ENABLE_FFMPEG "build ffmpeg" ON)
option(AVOX_ENABLE_FAAC "build faac" ON)
option(AVOX_ENABLE_FDKAAC "build fdk-aac" ON)
option(AVOX_ENABLE_FREETYPE "build freetype" ON)
# AVOX_ENABLE_CV / AVOX_ENABLE_ONNX / AVOX_ENABLE_TRANSLATION 已挪到 plugins/options.cmake(组件化)
# 组件(plugins/)总开关; 各组件 option(AVOX_ENABLE_OPENCV 等)见 plugins/options.cmake
option(AVOX_ENABLE_PLUGINS "build plugin components (opencv/onnx/sherpa/...)" ON)
option(AVOX_ENABLE_AGENT "build Agent client (LM Studio/OpenAI compatible VLM)" ON)
option(AVOX_ENABLE_CLI "build avox_cli tool" ON)
option(AVOX_ENABLE_GODOT "build Godot 4 GDExtension plugin (platform/godot/plugin, Windows only)" ON)
option(AVOX_ENABLE_UNITY "build Unity native plugin (platform/unity/plugin, Windows only)" ON)

# 组件 option 定义(须在 AVOXOptions / add_subdirectory(src) 之前, 保证 src 的 gate 如 inpaint 可见)
if(AVOX_ENABLE_PLUGINS)
    include(${CMAKE_SOURCE_DIR}/plugins/options.cmake)
endif()

option(AVOX_ENABLE_SWIG_PYTHON "build swig python" ON)
# _AvoxWrapper.pyd 走稳定 ABI (abi3): 一份 .pyd 通吃所有 Python>=floor (3.11), 不再版本锁死。
# ON=链 python3.lib + Py_LIMITED_API; OFF=回退旧的 python314.lib 版本锁死构建。
option(AVOX_PYTHON_STABLE_ABI "build _AvoxWrapper.pyd as stable-ABI (abi3): one .pyd for all Python>=floor" ON)

# AVOX_ENABLE_SAMPLES在windows平台默认打开
cmake_dependent_option(AVOX_ENABLE_SAMPLES
    "build samples (Windows only)"
    ON # 当条件满足时的默认值
    "CMAKE_SYSTEM_NAME STREQUAL Windows OR CMAKE_SYSTEM_NAME STREQUAL Linux" # 依赖条件表达式
    # 当条件不满足时的默认值
    OFF)

# 统一头文件搜索根: src + plugins, 使两目录下模块用一致的 <模块名>/<头> 引用,
# 模块在 src <-> plugins 迁移时 include 零改动
include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/plugins)

# 检查选项及对应库，配置相应库头文件，链接，以及宏定义
include(AVOXOptions)

# include(FFmpegBuild)
set(AVOX_DLL_TYPE "SHARED")

if(APPLE)
    set(AVOX_DLL_TYPE "STATIC")
endif()

if(ACV_BUILD_WASM)
    # 添加条件编译宏
    add_definitions(-DACV_BUILD_WASM=1)
    # WASM音视频渲染用SDL2
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_PTHREADS=1")
    set(AVOX_DLL_TYPE "STATIC")
    message(STATUS "编译为wasm:" ${CMAKE_CXX_FLAGS})
endif()

# 静态构建(iOS/WASM): plugin 源码编进 avox, 无 dll 边界。
# 定义 AVOX_ENABLE_STATIC -> AVOX_REGISTER_MODULE 走构造期注册(StaticLinkModule), 不导出 NewModule。
if(AVOX_DLL_TYPE STREQUAL "STATIC")
    add_compile_definitions(AVOX_ENABLE_STATIC)
endif()

# WEBRTC和ZLMediaKit使用MT静态运行时库
if(MSVC)
    set(CompilerFlags
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO)
    foreach(CompilerFlag ${CompilerFlags})
        message(STATUS "CompilerFlag ${CompilerFlag}: ${${CompilerFlag}}")
        string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
        message(STATUS "CompilerFlag ${CompilerFlag} replace: ${${CompilerFlag}}")
    endforeach()
    message(STATUS "msvc crt MT ")
endif()

# 库文件
add_subdirectory(src) 

# 插件(组件)模块: 独立编译为 avox_<name>.dll, 输出到 plugins/
if(AVOX_ENABLE_PLUGINS)
    include(avox_module)
    add_subdirectory(plugins)
endif()

# 测试用例
if(AVOX_ENABLE_SAMPLES)
    add_subdirectory(samples)
endif()

# GLSL文件
if(AVOX_ENABLE_VULKAN)
    add_subdirectory(glsl)
endif()

# Godot 4 GDExtension 插件 (Windows/Android; 依赖 3rdparty/godot-cpp 子模块)
if(AVOX_ENABLE_GODOT AND (WIN32 OR ANDROID))
    if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/godot-cpp/CMakeLists.txt")
        add_subdirectory(platform/godot/plugin)
    else()
        message(WARNING "AVOX_ENABLE_GODOT=ON 但缺 3rdparty/godot-cpp 子模块, 跳过插件 (git submodule update --init --recursive)")
    endif()
endif()

# Unity 原生插件 (Windows; avox_unity.dll 供 platform/unity/plugin/unity 的 com.avox.player UPM 包使用)
if(AVOX_ENABLE_UNITY AND (WIN32 OR ANDROID))
    add_subdirectory(platform/unity/plugin)
endif()

if(AVOX_ENABLE_SWIG)
    # swig
    add_subdirectory(swig)
endif()

# 导出给外部用户使用的C头文件,主要是抽像类/结构/C风格创建对象三种
copy_head("avox/AvoxVersion.h")
copy_head("avox/AvoxAudio.h")
copy_head("avox/AvoxBase.h")
copy_head("avox/AvoxBuffer.h")
copy_head("avox/AvoxCodec.h")
copy_head("avox/AvoxCore.h")
copy_head("avox/AvoxDef.h")
copy_head("avox/AvoxImage.h")
copy_head("avox/AvoxLayer.h")
copy_head("avox/AvoxLog.h")
copy_head("avox/AvoxPlayer.h")
# avox/AvoxAgent.h 已随旧 IAgentTool/IToolResult 一并删除:
# 工具现在是 avox_agent/core/ToolTypes.hpp 的 ToolDefinition (值 + 函数对象, 无类层次),
# 而跨语言边界走 avox_agent/export/AgentHostExport.h 的 C 接口。
copy_head("avox/AvoxTime.h")
copy_head("avox/AvoxVideo.h")
copy_head("avox/AvoxMuxer.h")
copy_head("avox/AvoxSource.h")
# 视觉 AI 纯接口 (IYoloDetector / IWatermarkRemoval / createYoloDetector 等, 随 avox.dll); IWatermarkRemoval 原在 AvoxModel.h
copy_head("avox/AvoxVision.h")
copy_head("avox_ffmpeg/FFExport.h")
copy_head("avox_vulkan/VkExport.h")
copy_head("avox_zlmediakit/ZlmExport.h")
# avox_webrtc 已迁 plugins/avox_webrtc; RtcExport.h(addRtcPlayerOb/removeRtcPlayerOb) 是插件导出,
# 核心导出 IRtcPlayer/createWebRtcPlayer/createZlTestSdpAgent/addRtcPlayerOb/removeRtcPlayerOb 已在 avox/AvoxPlayer.h
copy_head("avox_freetype/FreetypeExport.h")
# 平台相关
copy_head("avox_apple/IOSHelper.h")
copy_head("avox_windows/WinExport.h")
copy_head("avox_android/AndHelper.h")
copy_head("avox_egl/EglExport.h")
copy_head("avox_linux/LinuxHelper.h")
# inpaint: 核心纯接口 IWatermarkRemoval 在 avox/AvoxVision.h(随 avox.dll), C-API 在 plugin dll
# agent 导出层: IAgentHost / IAgentSession / ISessionObserver / IApprovalUi
# + createAgentHost / agentShellRun / runSkill
copy_head("avox_agent/AgentExport.h")
copy_head("avox/AvoxMath.h")
# hpp文件，供C++项目引用
copy_head("avox/layer/AvoxNode.hpp")
copy_head("avox_vulkan/VkTemplate.hpp")
# ============================== 单元测试 ==============================
option(AVOX_BUILD_TESTS "Build avox unit tests (doctest)" ON)
if(AVOX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
