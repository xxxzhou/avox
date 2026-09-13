# avox_ass 插件: ASS/SSA(后续含 PGS 位图)字幕 overlay 渲染
# 依赖策略: libass/fribidi/harfbuzz 在 3rdparty/library/ass/ 预编译(构建脚本
# script/ass/build_windows.py)。默认探测仓内产物, 找到即链接 libass 并定义
# AVOX_ASS_HAVE_LIBASS=1(真实渲染);否则只编骨架 —— AssOverlay::init() 返回
# false, 调用方运行期降级, 核心构建零污染。设计见 doc/plan/player/ASS字幕渲染计划.md
if(NOT AVOX_ENABLE_ASS)
  message(STATUS "avox_ass plugin: 未启用, 跳过")
  return()
endif()

# 默认探测 avc_library 的预编译产物(路径走根 CMake 的 AVOX_EXTERNAL_LIBRARY_DIR:
# -D 参数 > 环境变量 > 默认 ../avc_library; 可用 AVOX_ASS_DEPS_DIR 显式覆盖)
if(NOT AVOX_ASS_DEPS_DIR
   AND EXISTS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/windows/ass/lib/ass.lib")
  set(AVOX_ASS_DEPS_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/windows/ass")
endif()

set(_ass_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/AssModule.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AssOverlay.cpp)

if(AVOX_ASS_DEPS_DIR)
  # 预编译产物布局(avox-ass-deps dist/<platform>/): include/ + lib/ + bin/(Windows dll)
  set(_ass_inc ${AVOX_ASS_DEPS_DIR}/include)
  if(WIN32)
    set(_ass_libdir ${AVOX_ASS_DEPS_DIR}/lib)
    find_library(_ass_lib NAMES ass PATHS ${_ass_libdir} NO_DEFAULT_PATH)
    register_plugin(avox_ass DYNAMIC
      SOURCES ${_ass_sources}
      LIBS ${_ass_lib}
      # freetype/harfbuzz 已静态链入 ass-9.dll(避免与核心 avox_freetype 双副本);
      # fribidi LGPL 保持动态
      DEP_DLLS
        ${AVOX_ASS_DEPS_DIR}/bin/ass-9.dll
        ${AVOX_ASS_DEPS_DIR}/bin/fribidi-0.dll)
  else()
    # Linux/macOS/Android: 静态 .a 自包含, 无需拷贝 dll
    find_library(_ass_lib NAMES ass PATHS ${AVOX_ASS_DEPS_DIR}/lib NO_DEFAULT_PATH)
    register_plugin(avox_ass DYNAMIC
      SOURCES ${_ass_sources}
      LIBS ${_ass_lib})
  endif()
  target_include_directories(avox_ass PRIVATE ${_ass_inc})
  target_compile_definitions(avox_ass PRIVATE AVOX_ASS_HAVE_LIBASS=1)
else()
  # M1 骨架: 无外部依赖, 注册/查表/降级链路可独立冒烟
  register_plugin(avox_ass DYNAMIC SOURCES ${_ass_sources})
endif()
