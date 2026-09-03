
# 模块名
set(DllName avox)

# 更新全局变量
function(avox_update_cached name value)
  set("${name}" "${value}" CACHE INTERNAL "*** Internal ***" FORCE)
endfunction()

# 更新列表
function(avox_update_cached_list name)
  set(_tmp_list "${${name}}")
  list(APPEND _tmp_list "${ARGN}")
  list(REMOVE_DUPLICATES _tmp_list)
  avox_update_cached(${name} "${_tmp_list}")
endfunction()

# 非重复地向列表添加元素
function(avox_list_append_unique list_var)
    foreach(_item IN LISTS ARGN)
        list(FIND ${list_var} "${_item}" _index)
        if(_index EQUAL -1)
            list(APPEND ${list_var} "${_item}")
        endif()
    endforeach()
    set(${list_var} ${${list_var}} PARENT_SCOPE)
endfunction()

# 知道路径一次查找多个库
function(find_library_list lib_var lib_path)
  foreach(LIB_NAME ${ARGN})
    find_library(${LIB_NAME}_LOC NAMES ${LIB_NAME} PATHS ${${lib_path}})
    if(EXISTS ${${LIB_NAME}_LOC})
      list(APPEND ${lib_var} ${${LIB_NAME}_LOC})
    else()
      message(WARNING "Could not find library: ${LIB_NAME} in path: ${${lib_path}}")
    endif()
  endforeach()
  set(${lib_var} ${${lib_var}} PARENT_SCOPE)
endfunction()

# 添加子文件夹里文件并归类
function(add_sub_path relativePath HEADER_FILES SOURCE_FILELIST)
  string(REPLACE "/" "\\" filterPart ${relativePath})
  file(GLOB TEMP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/*.mm")
  file(GLOB TEMP_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/*.hpp")
  # IOS过滤掉 ._ 开头的源文件
  list(FILTER TEMP_SOURCE EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/\\._")
  # IOS过滤掉 ._ 开头的头文件
  list(FILTER TEMP_HEADER EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/${relativePath}/\\._")
  # 这是列表的操作方式    
  set(${HEADER_FILES} ${${HEADER_FILES}} ${TEMP_HEADER} PARENT_SCOPE)
  set(${SOURCE_FILELIST} ${${SOURCE_FILELIST}} ${TEMP_SOURCE} PARENT_SCOPE)
  source_group(${filterPart} FILES ${TEMP_HEADER} ${TEMP_SOURCE})
endfunction()

# 生成目录
function(avox_output targetname)
  # message(STATUS "output" ${targetname})
  set_target_properties(${targetname} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_INSTALL_PREFIX}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_INSTALL_PREFIX}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_INSTALL_PREFIX}
  )
endfunction(avox_output targetname)

# 查找目录复制
function(avox_run_module_copy modulepath)
  message(STATUS "copy to run path: ${modulepath}") 
  # 安卓平台生成的so文件在CMAKE_INSTALL_PREFIX下
  # 其他平台生成文件会自动加一个CMAKE_BUILD_TYPE目录
  if(ANDROID OR ONLY_LINUX) 
    file(COPY ${modulepath} DESTINATION "${CMAKE_INSTALL_PREFIX}")    
  else()
    file(COPY ${modulepath} DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}")
  endif()
endfunction(avox_run_module_copy lib)

# 导出给外部用户使用的C头文件,主要是抽像类/结构/C风格创建对象三种
function(copy_head HEAD_FILE)
    set(SRC_FILE "${CMAKE_SOURCE_DIR}/src/${HEAD_FILE}")
    set(DST_FILE "${CMAKE_BINARY_DIR}/install/include/${HEAD_FILE}")
    get_filename_component(DST_DIR "${DST_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${DST_DIR}")
    
    # 检查目标文件是否存在且内容相同
    if(EXISTS "${DST_FILE}")
        file(SHA256 "${SRC_FILE}" SRC_HASH)
        file(SHA256 "${DST_FILE}" DST_HASH)
        if(SRC_HASH STREQUAL DST_HASH)
            file(TIMESTAMP "${SRC_FILE}" SRC_TIME)
            file(TIMESTAMP "${DST_FILE}" DST_TIME)
            if(SRC_TIME LESS_EQUAL DST_TIME)
                return()
            endif()
        endif()
    endif()    
    file(COPY "${SRC_FILE}" DESTINATION "${DST_DIR}" FILES_MATCHING PATTERN "*")
endfunction()

# 把assets根据不同平台复制到不同目录
# title主要有glsl/image/fonts/modules
# SRC_FILES 源文件列表
# DEST_DIR 目标目录
function(avox_copy_assets DEST_DIR SRC_FILES)
    # 根据平台设置目标目录
    if(ANDROID)
        # Android平台：复制到assets目录
        set(FINAL_DEST_DIR "${CMAKE_SOURCE_DIR}/platform/android/AvoxJava/avox/assets/${DEST_DIR}")
    elseif(WIN32)
        # Windows平台：复制到安装目录的对应子目录
        set(FINAL_DEST_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/assets/${DEST_DIR}")
    elseif(ONLY_LINUX)
        # Linux平台：复制到系统共享目录
        set(FINAL_DEST_DIR "${CMAKE_INSTALL_PREFIX}/assets/${DEST_DIR}")
    endif()

    # 创建目标目录（预构建阶段）
    add_custom_command(TARGET avox PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FINAL_DEST_DIR}"
        COMMENT "Creating ${DEST_DIR} assets directory"
        COMMAND_EXPAND_LISTS VERBATIM)

    # 增量复制资源文件（构建后阶段）
    if(WIN32)
        # Windows使用robocopy实现增量复制
        # /MIR 镜像目录 /XO 排除较旧文件 /NDL 无目录列表 /NJH 无作业头 /NJS 无作业摘要
        add_custom_command(TARGET avox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --blue "Copying ${DEST_DIR} files (incremental)..."
            COMMAND if exist "${SRC_FILES}" (robocopy "${SRC_FILES}" "${FINAL_DEST_DIR}" /MIR /XO /NDL /NJH /NJS /R:0 /W:0 || cmd /c "exit 0") else (${CMAKE_COMMAND} -E cmake_echo_color --yellow "Source directory not found, skipping: ${SRC_FILES}")
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copied ${DEST_DIR} successfully"
            COMMENT "Copying ${DEST_DIR} assets"
            VERBATIM)
    else()
        # 其他平台使用copy_directory
        add_custom_command(TARGET avox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --blue "Copying ${DEST_DIR} files (incremental)..."
            COMMAND test -d "${SRC_FILES}" && ${CMAKE_COMMAND} -E copy_directory "${SRC_FILES}" "${FINAL_DEST_DIR}" || ${CMAKE_COMMAND} -E cmake_echo_color --yellow "Source directory not found, skipping: ${SRC_FILES}"
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copied ${DEST_DIR} successfully"
            COMMENT "Copying ${DEST_DIR} assets"
            VERBATIM)
    endif()

    # 输出调试信息
    message(STATUS "Configured ${DEST_DIR} assets copying (incremental):")
    message(STATUS "  Destination: ${FINAL_DEST_DIR}")
endfunction()

# 把ios的assets复制到BUNDLE_NAME下
# BUNDLE_NAME是目标Bundle
# title主要有glsl/image/fonts/modules
# BUNDLE_PATH BUNDLE对应的路径
# SRC_FILES 源文件列表
function(avox_copy_ios_assets BUNDLE_NAME BUNDLE_PATH title SRC_FILES)
  # 设置资源目标目录
  set(RESOURCES_DIR "${BUNDLE_PATH}/${title}")  
  # 处理SRC_FILES列表
  if(NOT SRC_FILES)
      message(WARNING "avox_copy_ios_assets: No source files provided for ${title}")
      return()
  endif()
# 创建资源目录，添加日志和错误处理
  add_custom_command(TARGET ${BUNDLE_NAME} PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Created directory: ${RESOURCES_DIR}"
      COMMENT "Creating ${title} directory for iOS bundle"
      COMMAND_EXPAND_LISTS VERBATIM) 
  # 直接复制整个文件列表到目标目录
  add_custom_command(TARGET ${BUNDLE_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --blue "Copying ${title} files to ${RESOURCES_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${SRC_FILES}" "${RESOURCES_DIR}"
      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copied ${title} files successfully"
      COMMENT "Copying ${title} files to iOS bundle"
      COMMAND_EXPAND_LISTS VERBATIM) 
  # 输出调试信息
  message(STATUS "Configured iOS ${title} assets copying: ${SRC_FILES}")
endfunction(avox_copy_ios_assets)
