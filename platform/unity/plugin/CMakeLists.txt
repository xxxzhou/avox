# platform/unity/plugin/CMakeLists.txt — avox_unity (Unity 原生插件)
# 随 avox 一起构建: 由顶层 CMakeLists add_subdirectory 引入 (AVOX_ENABLE_UNITY, Windows)。
# 产物 avox_unity.dll 输出到 avox install 树 install/<CPU>/<CONFIG>/,
# 由 deploy_unity.ps1 复制到 Unity 工程 Packages/com.avox.player/Runtime/Plugins/Windows/x86_64/。

# ── avox 头 / volk / khronos (同 godot 插件, 走 avox 仓库 3rdparty) ──
# volk 以 VOLK_IMPLEMENTATION 编进本插件 (src/volk_impl.c), 不链 vulkan-1.lib

set(_UNITY_SRCS
    src/AvoxUnityApi.cpp
    src/PlayerBridge.cpp
    src/SourceBridge.cpp
    src/GpuPassthrough.cpp
    src/volk_impl.c
)
# WebRTC 桥仅 Windows: Android 构建 AVOX_ENABLE_WEBRTC=OFF, 核心无 webrtc 能力
if(WIN32)
    list(APPEND _UNITY_SRCS src/RtcPlayerBridge.cpp)
endif()
add_library(avox_unity SHARED ${_UNITY_SRCS})

target_include_directories(avox_unity PRIVATE
    ${CMAKE_SOURCE_DIR}/src                # avox 头 (avox/AvoxPlayer.h 等)
    ${CMAKE_SOURCE_DIR}/3rdparty/khronos   # vulkan/vulkan.h
    ${CMAKE_SOURCE_DIR}/3rdparty/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/src        # 本插件头 + unity/ 官方插件头
)

# 链 avox 核心 SDK (import lib); 消费 avox.dll 不定义 AVOX_EXPORT_DEFINE, 保持 dllimport
target_link_libraries(avox_unity PRIVATE avox)

# Vulkan 走 volk 动态加载; 平台宏给扩展结构/函数声明 (Win32: NT 句柄查询,
# Android: AHB 导入 + Unity 设备扩展注入)
if(WIN32)
    set(_UNITY_DEFS VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
else()
    set(_UNITY_DEFS VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR)
endif()
target_compile_definitions(avox_unity PRIVATE ${_UNITY_DEFS})

# 继承 avox 内部类触发 C4251/C4275, 无害; PDB 配置
if(MSVC)
    target_compile_options(avox_unity PRIVATE /wd4251 /wd4275 /Zi /Zo)
    target_link_options(avox_unity PRIVATE /DEBUG:FULL)
endif()

# 输出到 avox install 树, 与 avox 库同级; Windows 去前缀 (Unity DllImport("avox_unity")),
# Android 保留 lib 前缀 (DllImport("avox_unity") 自动映射 libavox_unity.so)
set(OUTPUT_DIR "${CMAKE_INSTALL_PREFIX}/$<CONFIG>")
if(WIN32)
    set_target_properties(avox_unity PROPERTIES PREFIX "")
endif()
set_target_properties(avox_unity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)
