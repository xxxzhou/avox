# avox_ass 插件: ASS/SSA(后续含 PGS 位图)字幕 overlay 渲染
# 依赖策略: libass/fribidi/harfbuzz 在独立仓 avox-ass-deps 预编译(三平台产物)。
# AVOX_ASS_DEPS_DIR 指向产物目录时链接 libass 并定义 AVOX_ASS_HAVE_LIBASS=1
# (真实渲染);否则只编骨架 —— AssOverlay::init() 返回 false, 调用方运行期降级,
# 核心构建零污染。设计见 doc/plan/player/ASS字幕渲染计划.md
if(NOT AVOX_ENABLE_ASS)
  message(STATUS "avox_ass plugin: 未启用, 跳过")
  return()
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
      DEP_DLLS
        ${AVOX_ASS_DEPS_DIR}/bin/ass-9.dll
        ${AVOX_ASS_DEPS_DIR}/bin/fribidi-0.dll
        ${AVOX_ASS_DEPS_DIR}/bin/harfbuzz.dll
        ${AVOX_ASS_DEPS_DIR}/bin/freetype.dll)
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
