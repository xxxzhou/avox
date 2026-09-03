set(FFmpeg_INC_SEARCH_PATH ${AVOX_TRDPARTY_LIBRAY}/ffmpeg/include)
set(FFmpeg_LIB_SEARCH_PATH ${AVOX_TRDPARTY_LIBRAY}/ffmpeg/bin ${AVOX_TRDPARTY_LIBRAY}/ffmpeg/lib)

message(STATUS "ffmpeg include:" ${FFmpeg_INC_SEARCH_PATH})
message(STATUS "ffmpeg libs:" ${FFmpeg_LIB_SEARCH_PATH})

set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARIES)
set(FFMPEG_BINARYS)

macro(ffmepg_find_component component header version)
    string(TOUPPER "${component}" UCOMPONENT)
    set(FFMPEG_${UCOMPONENT}_FOUND FLASE)
    set(FFmpeg_${component}_FOUND FLASE)
    find_path(FFMPEG_${component}_INCLUDE_DIR NAMES "lib${component}/${header}" HINTS ${FFmpeg_INC_SEARCH_PATH} PATH_SUFFIXES)
    find_library(FFMPEG_${component}_LIBRARY NAMES "${component}" "lib${component}" HINTS ${FFmpeg_LIB_SEARCH_PATH} PATH_SUFFIXES)
    set(FFMPEG_${UCOMPONENT}_INCLUDE_DIRS ${FFMPEG_${component}_INCLUDE_DIR})
    set(FFMPEG_${UCOMPONENT}_LIBRARIES ${FFMPEG_${component}_LIBRARY})

    message(STATUS "ffmpeg ${component} include: " ${FFMPEG_${component}_INCLUDE_DIR})
    message(STATUS "ffmpeg ${component} libs: " ${FFMPEG_${component}_LIBRARY})
    # https://cmake.org/cmake/help/v3.0/command/if.html
    # if直接填写变量 
    if(FFMPEG_${component}_INCLUDE_DIR AND FFMPEG_${component}_LIBRARY)
        set(FFMPEG_${UCOMPONENT}_FOUND TRUE)
        set(FFmpeg_${component}_FOUND TRUE)

        # 添加到FFMPEG_INCLUDE_DIRS,然后去重
        list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${component}_INCLUDE_DIR})
        list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIRS}")

        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${component}_LIBRARY})
        list(REMOVE_DUPLICATES FFMPEG_LIBRARIES)
        set(FFMPEG_LIBRARIES "${FFMPEG_LIBRARIES}")

        set(FFMPEG_${UCOMPONENT}_VERSION_STRING "unknown")
        set(_vfile "${FFMPEG_${component}_INCLUDE_DIR}/lib${component}/${version}")
        if(EXISTS "${_vfile}")
            file(STRINGS "${_vfile}" _version_parse REGEX "^.*VERSION_(MAJOR|MINOR|MICRO)[ \t]+[0-9]+[ \t]*$")
            string(REGEX REPLACE ".*VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" _major "${_version_parse}")
            set(FFMPEG_${UCOMPONENT}_VERSION_MAJOR "${_major}")
            if(WIN32)
                find_file(FFMPEG_${component}_BINARYS NAME "${component}-${_major}.dll" HINTS ${FFmpeg_LIB_SEARCH_PATH} PATH_SUFFIXES)
                message(STATUS "ffmpeg ${component} dll: " "${FFMPEG_${component}_BINARYS}")
                list(APPEND FFMPEG_BINARYS "${FFMPEG_${component}_BINARYS}")
                set(FFMPEG_BINARYS "${FFMPEG_BINARYS}" PARENT_SCOPE)
            endif()
        else()
            message(STATUS "Failed parsing FFmpeg ${component} version")
        endif()
    endif()
endmacro()

ffmepg_find_component("avutil" "avutil.h" "version.h")
ffmepg_find_component("avformat" "avformat.h" "version_major.h")
ffmepg_find_component("avcodec" "avcodec.h" "version_major.h")
ffmepg_find_component("swresample" "swresample.h" "version_major.h")
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    FOUND_VAR FFMPEG_FOUND
    REQUIRED_VARS FFMPEG_AVCODEC_LIBRARIES FFMPEG_AVCODEC_INCLUDE_DIRS
    VERSION_VAR FFMPEG_AVCODEC_VERSION_STRING
    HANDLE_COMPONENTS)

if(FFMPEG_FOUND)
    if(WIN32)
        message(STATUS "ffmpeg dlls: " ${FFMPEG_BINARYS})
        # 这里如何avox_run_module_copy(${FFMPEG_BINARYS})
        # 会发现只传入了第一个目录
        avox_run_module_copy("${FFMPEG_BINARYS}")
    else()
        avox_run_module_copy("${FFMPEG_LIBRARIES}")
    endif()
endif()
