# avox_module.cmake —— 插件(组件)动态/静态编译封装
#
# 用法(plugins/<name>/CMakeLists.txt 内):
#   register_plugin(avox_opencv  DYNAMIC LIBS ${OpenCV_LIBRARIES} DEP_DLLS <dll...>)
#   register_plugin(avox_cv DYNAMIC DEPS avox_onnx avox_opencv LIBS ...)
#
# DYNAMIC: 独立 avox_<name>.dll/.so, 输出到 ${CMAKE_INSTALL_PREFIX}/plugins/
# STATIC : 源码仍由 src/CMakeLists.txt 的 add_sub_path 编进 avox(占位, 不建 target)

# plugin 产物输出到 avox.dll 同级的 plugins/ 子目录
# $<CONFIG> 让多配置生成器(MSVC)输出到 ${CMAKE_INSTALL_PREFIX}/<CONFIG>/plugins/,
# 与 avox.dll(${CMAKE_INSTALL_PREFIX}/<CONFIG>/) 同级, 运行期 GetModuleFileName(avox.dll)/plugins 即匹配
function(avox_plugin_output targetname)
  set(_plugin_dir "${CMAKE_INSTALL_PREFIX}/$<CONFIG>/plugins")
  set_target_properties(${targetname} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_plugin_dir}"
    LIBRARY_OUTPUT_DIRECTORY "${_plugin_dir}"
    ARCHIVE_OUTPUT_DIRECTORY "${_plugin_dir}")
  message(STATUS "register_plugin(DYNAMIC): ${targetname} -> ${_plugin_dir}")
endfunction()

# 把 plugin 运行期依赖的第三方 dll 复制到 plugins/(Windows), 使 plugins/ 自包含
# (LoadLibraryEx 完整路径 + LOAD_WITH_ALTERED_SEARCH_PATH 时从 plugin 同目录找依赖)
function(avox_plugin_copy_deps targetname)
  if(NOT WIN32)
    return()
  endif()
  set(_dst "${CMAKE_INSTALL_PREFIX}/$<CONFIG>/plugins")
  add_custom_command(TARGET ${targetname} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
    COMMENT "Ensure plugins dir: ${_dst}")
  foreach(_dep ${ARGN})
    if(EXISTS "${_dep}")
      add_custom_command(TARGET ${targetname} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dep}" "${_dst}"
        COMMENT "Copy plugin dep: ${_dep}")
    endif()
  endforeach()
endfunction()

# 注册插件: name + (STATIC|DYNAMIC) + [DEPS ...] + [LIBS ...] + [DEP_DLLS ...]
function(register_plugin name)
  set(_mode "${ARGV1}")
  set(_libs "")
  set(_deps "")
  set(_dep_dlls "")
  set(_sources "")
  set(_kw "")
  foreach(_arg ${ARGV})
    if(_arg STREQUAL "STATIC" OR _arg STREQUAL "DYNAMIC")
      continue()
    elseif(_arg STREQUAL "LIBS")
      set(_kw "LIBS")
      continue()
    elseif(_arg STREQUAL "DEPS")
      set(_kw "DEPS")
      continue()
    elseif(_arg STREQUAL "DEP_DLLS")
      set(_kw "DEP_DLLS")
      continue()
    elseif(_arg STREQUAL "SOURCES")
      set(_kw "SOURCES")
      continue()
    elseif(_arg STREQUAL "${name}")
      continue()
    endif()
    if(_kw STREQUAL "LIBS")
      list(APPEND _libs "${_arg}")
    elseif(_kw STREQUAL "DEPS")
      list(APPEND _deps "${_arg}")
    elseif(_kw STREQUAL "DEP_DLLS")
      list(APPEND _dep_dlls "${_arg}")
    elseif(_kw STREQUAL "SOURCES")
      list(APPEND _sources "${_arg}")
    endif()
  endforeach()

  if(_mode STREQUAL "STATIC")
    # STATIC: 源码仍由 src/CMakeLists.txt 的 add_sub_path 编进 avox, 此处仅占位/日志
    message(STATUS "register_plugin(STATIC): ${name} (compiled into avox via add_sub_path)")
    return()
  endif()

  # DYNAMIC: 建独立 SHARED target
  if(_sources)
    set(_src ${_sources})
  else()
    file(GLOB _src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
  endif()
  file(GLOB _hdr "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
  add_library(${name} SHARED ${_src} ${_hdr})
  # 链 avox(import lib, 拿 IModule/IOption 等导出) + 第三方库
  target_link_libraries(${name} PRIVATE avox ${_libs})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/plugins)
  # 铁律: 只传 AVOX_PLUGIN_BUILDING(导出 NewModule/GetModuleABI), 绝不传 AVOX_EXPORT_DEFINE
  # (否则 IModule 被当 dllexport, 跨 dll 虚表错乱)
  target_compile_definitions(${name} PRIVATE AVOX_PLUGIN_BUILDING)
  # 插件继承 avox.dll 内部类(AVOX_EXPORT), std::成员和非导出基类触发 C4251/C4275, 无害
  if(MSVC)
    target_compile_options(${name} PRIVATE /wd4251 /wd4275)
  endif()
  avox_plugin_output(${name})
  if(_dep_dlls)
    avox_plugin_copy_deps(${name} ${_dep_dlls})
  endif()
endfunction()
