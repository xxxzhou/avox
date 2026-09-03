# FindOpenVINO.cmake
# 查找 OpenVINO C++ runtime (从 pip wheel 提取的最小 GPU 集)
#
# 用法:
#   find_package(OpenVINO [REQUIRED])
#
# 定义变量:
#   OpenVINO_FOUND
#   OPENVINO_INCLUDE_DIRS - 头文件目录 (openvino/ oneapi/ tbb/)
#   OPENVINO_LIBRARIES    - openvino.lib (链接期)
#   OPENVINO_DLLS         - 运行时 dll + cache.json (Windows, DEP_DLLS 用)
#
# 前置: python script/openvino/extract_openvino.py 提取 runtime 到 avc_library

include(FindPackageHandleStandardArgs)

set(OPENVINO_VERSION "2026.2")

# 搜索路径 (同 FindONNX: AVOX_EXTERNAL_LIBRARY_DIR 优先)
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    set(OPENVINO_SEARCH_PATHS
        ${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library
        ${PROJECT_SOURCE_DIR}/3rdparty/library
    )
else()
    set(OPENVINO_SEARCH_PATHS
        ${PROJECT_SOURCE_DIR}/avc_library/3rdparty/library
        ${PROJECT_SOURCE_DIR}/3rdparty/library
    )
endif()

# OpenVINO 主要 Windows/Linux + Intel 硬件 (Android/iOS/WebAssembly 不启用)
set(OPENVINO_DIR "")
if(WIN32)
    set(OPENVINO_DIR_NAMES
        "windows/openvino/openvino-win-x64-${OPENVINO_VERSION}"
    )
elseif(UNIX AND NOT ANDROID AND NOT IOS)
    set(OPENVINO_DIR_NAMES
        "linux/openvino/openvino-linux-x86_64-${OPENVINO_VERSION}"
    )
endif()

foreach(dir_name ${OPENVINO_DIR_NAMES})
    foreach(search_path ${OPENVINO_SEARCH_PATHS})
        if(EXISTS "${search_path}/${dir_name}")
            set(OPENVINO_DIR "${search_path}/${dir_name}")
            break()
        endif()
    endforeach()
    if(OPENVINO_DIR)
        break()
    endif()
endforeach()

if(OPENVINO_DIR)
    set(OPENVINO_INCLUDE_DIRS "${OPENVINO_DIR}/include")
    set(OPENVINO_LIB_DIR "${OPENVINO_DIR}/lib")

    find_library(OPENVINO_LIBRARY
        NAMES openvino
        PATHS ${OPENVINO_LIB_DIR}
        NO_DEFAULT_PATH
    )
    if(OPENVINO_LIBRARY)
        set(OPENVINO_LIBRARIES ${OPENVINO_LIBRARY})
    endif()

    # 运行时: dll + cache.json (openvino.dll 同目录, LoadLibrary 加载 plugin/tbb)
    set(_openvino_runtime_files
        openvino.dll                       # 主运行时
        openvino_onnx_frontend.dll         # 读 .onnx (Real-ESRGAN)
        openvino_ir_frontend.dll
        openvino_intel_gpu_plugin.dll      # Intel iGPU
        openvino_intel_cpu_plugin.dll      # CPU 回退 (Intel/AMD 通用)
        openvino_auto_plugin.dll
        openvino_hetero_plugin.dll
        tbb12.dll                          # TBB (openvino.dll 依赖)
        tbbbind_2_5.dll
        tbbmalloc.dll
        tbbmalloc_proxy.dll
        cache.json                         # GPU 内核缓存
    )
    foreach(f ${_openvino_runtime_files})
        if(EXISTS "${OPENVINO_LIB_DIR}/${f}")
            list(APPEND OPENVINO_DLLS "${OPENVINO_LIB_DIR}/${f}")
        endif()
    endforeach()
endif()

find_package_handle_standard_args(OpenVINO
    REQUIRED_VARS OPENVINO_LIBRARIES OPENVINO_INCLUDE_DIRS
)

if(OpenVINO_FOUND)
    message(STATUS "OpenVINO found:")
    message(STATUS "  Version: ${OPENVINO_VERSION}")
    message(STATUS "  Include: ${OPENVINO_INCLUDE_DIRS}")
    message(STATUS "  Library: ${OPENVINO_LIBRARIES}")
    list(LENGTH OPENVINO_DLLS _dll_count)
    message(STATUS "  Runtime files: ${_dll_count}")
endif()

# 导入目标
if(OpenVINO_FOUND AND NOT TARGET OpenVINO::Runtime)
    add_library(OpenVINO::Runtime SHARED IMPORTED)
    set_target_properties(OpenVINO::Runtime PROPERTIES
        IMPORTED_IMPLIB "${OPENVINO_LIBRARIES}"
        IMPORTED_LOCATION "${OPENVINO_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENVINO_INCLUDE_DIRS}"
        INTERFACE_COMPILE_FEATURES "cxx_std_17"
    )
endif()

mark_as_advanced(OPENVINO_INCLUDE_DIRS OPENVINO_LIBRARIES OPENVINO_DLLS)
