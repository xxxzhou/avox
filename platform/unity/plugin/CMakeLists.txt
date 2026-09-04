# platform/unity/plugin/CMakeLists.txt — avox_unity (Unity 原生插件)
# 随 avox 一起构建: 由顶层 CMakeLists add_subdirectory 引入 (AVOX_ENABLE_UNITY, Windows)。
# 产物 avox_unity.dll 输出到 avox install 树 install/<CPU>/<CONFIG>/,
# 由 deploy_unity.ps1 复制到 Unity 工程 Packages/com.avox.player/Runtime/Plugins/Windows/x86_64/。

# ── avox 头 / volk / khronos (同 godot 插件, 走 avox 仓库 3rdparty) ──
# volk 以 VOLK_IMPLEMENTATION 编进本插件 (src/volk_impl.c), 不链 vulkan-1.lib

add_library(avox_unity SHARED
    src/AvoxUnityApi.cpp
    src/PlayerBridge.cpp
    src/GpuPassthrough.cpp
    src/volk_impl.c
)

target_include_directories(avox_unity PRIVATE
    ${CMAKE_SOURCE_DIR}/src                # avox 头 (avox/AvoxPlayer.h 等)
    ${CMAKE_SOURCE_DIR}/3rdparty/khronos   # vulkan/vulkan.h
    ${CMAKE_SOURCE_DIR}/3rdparty/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/src        # 本插件头 + unity/ 官方插件头
)

# 链 avox 核心 SDK (import lib); 消费 avox.dll 不定义 AVOX_EXPORT_DEFINE, 保持 dllimport
target_link_libraries(avox_unity PRIVATE avox)

# Vulkan 走 volk 动态加载; Win32 平台宏给 vkGetMemoryWin32HandlePropertiesKHR 等
target_compile_definitions(avox_unity PRIVATE
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_WIN32_KHR
)

# 继承 avox 内部类触发 C4251/C4275, 无害; PDB 配置
if(MSVC)
    target_compile_options(avox_unity PRIVATE /wd4251 /wd4275 /Zi /Zo)
    target_link_options(avox_unity PRIVATE /DEBUG:FULL)
endif()

# 输出到 avox install 树, 与 avox 库同级; Windows 去前缀 (Unity DllImport("avox_unity"))
set(OUTPUT_DIR "${CMAKE_INSTALL_PREFIX}/$<CONFIG>")
set_target_properties(avox_unity PROPERTIES PREFIX "")
set_target_properties(avox_unity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)
