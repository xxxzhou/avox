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
# WIN32 守卫: android/apple 交叉构建宿主也是 Windows 盘, 不守卫会抢先命中 windows 产物
if(NOT AVOX_ASS_DEPS_DIR
   AND WIN32
   AND EXISTS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/windows/ass/lib/ass.lib")
  set(AVOX_ASS_DEPS_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/windows/ass")
endif()
if(NOT AVOX_ASS_DEPS_DIR
   AND ANDROID
   AND EXISTS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/android/ass/lib/libass.a")
  set(AVOX_ASS_DEPS_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/android/ass")
endif()
if(NOT AVOX_ASS_DEPS_DIR
   AND APPLE
   AND NOT CMAKE_SYSTEM_NAME MATCHES "iOS"
   AND EXISTS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/darwin/ass/lib/libass.a")
  set(AVOX_ASS_DEPS_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/darwin/ass")
endif()
if(NOT AVOX_ASS_DEPS_DIR
   AND CMAKE_SYSTEM_NAME MATCHES "iOS"
   AND EXISTS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/ios/ass/lib/libass.a")
  set(AVOX_ASS_DEPS_DIR "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/ios/ass")
endif()

set(_ass_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/AssModule.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AssOverlay.cpp
  # 编码自愈(GBK→UTF-8/路径 openFileUtf8)与核心同源: 插件不链 avox.dll,
  # 该 TU 无 avox 内部依赖可直接同编(测试目标同款用法)
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/avox/subtitle/CharsetConvert.cpp)

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
    # Linux/macOS/Android/iOS: 静态 .a 自包含, 无需拷贝 dll;
    # Apple 还要补 CoreText 后端的系统框架(libass.a 内只留引用)
    set(_ass_extra_libs "")
    if(APPLE)
      set(_ass_extra_libs "-framework CoreText" "-framework CoreGraphics"
          "-framework Foundation")
    endif()
    find_library(_ass_lib NAMES ass PATHS ${AVOX_ASS_DEPS_DIR}/lib NO_DEFAULT_PATH)
    # libass.a 不内嵌依赖: harfbuzz/freetype/fribidi 按 libass 的引用序显式链接
    # (Windows 动态模式无此问题, 依赖已链入 ass-9.dll)
    set(_ass_dep_libs "")
    foreach(_dep harfbuzz freetype fribidi)
      find_library(_ass_dep_${_dep} NAMES ${_dep}
                   PATHS ${AVOX_ASS_DEPS_DIR}/lib NO_DEFAULT_PATH)
      if(_ass_dep_${_dep})
        list(APPEND _ass_dep_libs ${_ass_dep_${_dep}})
      endif()
    endforeach()
    register_plugin(avox_ass DYNAMIC
      SOURCES ${_ass_sources}
      LIBS ${_ass_lib} ${_ass_dep_libs} ${_ass_extra_libs})
  endif()
  target_include_directories(avox_ass PRIVATE ${_ass_inc})
  target_compile_definitions(avox_ass PRIVATE AVOX_ASS_HAVE_LIBASS=1)
else()
  # M1 骨架: 无外部依赖, 注册/查表/降级链路可独立冒烟
  register_plugin(avox_ass DYNAMIC SOURCES ${_ass_sources})
endif()
