# platform/godot/plugin/CMakeLists.txt — avox_godot (Godot 4 GDExtension 插件)
# 随 avox 一起构建: 由顶层 CMakeLists add_subdirectory 引入 (AVOX_ENABLE_GODOT, Windows/Android)。
# 产物输出到 avox install 树 install/<CPU>/<CONFIG>/, 与 avox 库同级。

# ── godot-cpp (avox 根 3rdparty 子模块) ───────────────
set(GODOT_CPP_DIR "${CMAKE_SOURCE_DIR}/3rdparty/godot-cpp" CACHE PATH "godot-cpp directory")
if(NOT EXISTS "${GODOT_CPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp not found. Run: git submodule update --init --recursive")
endif()
set(GODOTCPP_GENERATE_API_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(GODOTCPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${GODOT_CPP_DIR} godot-cpp EXCLUDE_FROM_ALL)

# avox 全局已把 MSVC 编译 flags 替换为 /MT (静态 CRT), godot-cpp 一并继承;
# 这里显式钉死目标级 runtime, 防配置漂移导致 LNK2038。
if(MSVC AND TARGET godot-cpp)
    set_target_properties(godot-cpp PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ── 插件目标 ─────────────────────────────────────────
# 插件层只保留播放级能力 (播放/WebRTC/采集/录制); 模型类 (Stt/Tts/Face/
# VideoFace/Body/Agent) 不进插件, 数字人栈走独立交付。
# 平台源: volk 平台胶水两版; voice.cpp(热键/麦克风采集/文本注入, 纯采集无模型) Win32 专属
if(WIN32)
    set(_PLATFORM_SRCS src/volk_win32.c src/voice.cpp)
    set(_PLATFORM_DEFS VK_USE_PLATFORM_WIN32_KHR)
else()
    set(_PLATFORM_SRCS src/volk_android.c)
    set(_PLATFORM_DEFS VK_USE_PLATFORM_ANDROID_KHR)
endif()

add_library(avox_godot SHARED
    src/godot_init.cpp
    src/player.cpp
    src/source_player.cpp
    src/source_probe.cpp
    src/recorder.cpp
    src/surface.cpp
    src/audio.cpp
    src/option.cpp
    src/rtc_player.cpp
    ${_PLATFORM_SRCS}
)

target_include_directories(avox_godot PRIVATE
    ${CMAKE_SOURCE_DIR}/src                # avox 头 (avox/AvoxPlayer.h 等)
    ${CMAKE_SOURCE_DIR}/3rdparty/khronos   # vulkan/vulkan.h
    ${CMAKE_SOURCE_DIR}/3rdparty/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链 avox 核心 SDK (Windows: import lib / Android: libavox.so) + godot-cpp;
# Windows 下另需 psapi (EnumProcessModules)
target_link_libraries(avox_godot PRIVATE avox godot::cpp)
if(WIN32)
    target_link_libraries(avox_godot PRIVATE psapi)
endif()

# 消费 avox.dll: 不定义 AVOX_EXPORT_DEFINE (AvoxDef.h 里那会变成 dllexport),
# 保持 dllimport 才对 —— 同 avox_module 铁律。
# Vulkan 走 volk 动态加载。
target_compile_definitions(avox_godot PRIVATE
    VK_NO_PROTOTYPES
    ${_PLATFORM_DEFS}
)

# 继承 avox 内部类触发 C4251/C4275, 无害; MSVC 专属告警抑制与 PDB 配置
if(MSVC)
    target_compile_options(avox_godot PRIVATE /wd4251 /wd4275 /Zi /Zo)
    target_link_options(avox_godot PRIVATE /DEBUG:FULL)
endif()

# 输出到 avox install 树, 与 avox 库同级 (install/<CPU>/<CONFIG>/);
# Android 保持 lib 前缀 (Godot 约定 libavox_godot.so), Windows 去 avox_godot.dll 前缀
set(OUTPUT_DIR "${CMAKE_INSTALL_PREFIX}/$<CONFIG>")
if(WIN32)
    set_target_properties(avox_godot PROPERTIES PREFIX "")
else()
    set_target_properties(avox_godot PROPERTIES PREFIX "lib")
endif()
set_target_properties(avox_godot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)

# ── .gdextension ──────────────────────────────────
# 项目级配置 (res://addons/avox_godot/bin/... 引用), 仓库内静态文件:
#   platform/godot/plugin/avox_godot.gdextension
# 由 deploy_godot.ps1 复制到 addons。故意不入 Release 运行目录 —— 否则 bin junction 部署时
# Godot 会把 Release 里的副本也扫到 (递归扫 res://), 同一个 entry_symbol 加载两次 → 类重复注册。
