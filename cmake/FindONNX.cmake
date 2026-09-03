# FindONNX.cmake
# 查找 ONNX Runtime 库
#
# 用法:
#   find_package(ONNX [REQUIRED])
#
# 定义变量:
#   ONNX_FOUND        - 是否找到
#   ONNXRUNTIME_INCLUDE_DIRS - 头文件目录
#   ONNXRUNTIME_LIBRARIES    - 库文件
#   ONNXRUNTIME_DLLS         - DLL 文件 (Windows)
#   ONNXRUNTIME_IS_STATIC    - 是否静态库
#
# 环境变量:
#   ONNXRUNTIME_DIR - 自定义 ONNX Runtime 安装路径

include(FindPackageHandleStandardArgs)

# 版本
set(ONNXRUNTIME_VERSION "1.23.2")

# 检测是否使用静态库
set(ONNXRUNTIME_IS_STATIC OFF)

# 默认搜索路径
# 优先使用 AVOX_EXTERNAL_LIBRARY_DIR (在根 CMakeLists.txt 中定义)
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    set(ONNXRUNTIME_SEARCH_PATHS
        $ENV{ONNXRUNTIME_DIR}
        ${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library
        ${PROJECT_SOURCE_DIR}/3rdparty/library
    )
else()
    # 向后兼容
    set(ONNXRUNTIME_SEARCH_PATHS
        $ENV{ONNXRUNTIME_DIR}
        ${PROJECT_SOURCE_DIR}/avc_library/3rdparty/library
        ${PROJECT_SOURCE_DIR}/3rdparty/library
    )
endif()

# ============== 平台检测 ==============

if(ANDROID)
    # ---------- Android ----------
    set(ONNXRUNTIME_IS_STATIC ON)

    # 检测架构
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(ONNXRUNTIME_ARCH "arm64-v8a")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(ONNXRUNTIME_ARCH "armeabi-v7a")
    else()
        message(WARNING "ONNX Runtime: 不支持的 Android 架构 ${ANDROID_ABI}")
    endif()

    # 搜索路径
    set(ONNXRUNTIME_DIR_NAMES
        "android/onnxruntime/onnxruntime-android-${ONNXRUNTIME_ARCH}-static_lib-${ONNXRUNTIME_VERSION}"
        "android/onnxruntime/onnxruntime-android-${ONNXRUNTIME_ARCH}-${ONNXRUNTIME_VERSION}"
    )

    foreach(dir_name ${ONNXRUNTIME_DIR_NAMES})
        foreach(search_path ${ONNXRUNTIME_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(ONNXRUNTIME_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(ONNXRUNTIME_DIR)
            break()
        endif()
    endforeach()

    if(ONNXRUNTIME_DIR)
        set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_DIR}/include")
        set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_DIR}/lib")

        # 静态库
        find_library(ONNXRUNTIME_LIBRARY
            NAMES onnxruntime
            PATHS ${ONNXRUNTIME_LIB_DIR}
            NO_DEFAULT_PATH
        )

        if(ONNXRUNTIME_LIBRARY)
            set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIBRARY})
        endif()
    endif()

elseif(IOS)
    # ---------- iOS ----------
    # iOS 使用 framework
    set(ONNXRUNTIME_IS_STATIC OFF)

    # 检测架构
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(ONNXRUNTIME_ARCH "arm64")
    else()
        set(ONNXRUNTIME_ARCH "x64")
    endif()

    # 搜索路径
    set(ONNXRUNTIME_DIR_NAMES
        "ios/onnxruntime/onnxruntime-ios-${ONNXRUNTIME_ARCH}-${ONNXRUNTIME_VERSION}"
        "ios/onnxruntime/onnxruntime.xcframework"
    )

    foreach(dir_name ${ONNXRUNTIME_DIR_NAMES})
        foreach(search_path ${ONNXRUNTIME_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(ONNXRUNTIME_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(ONNXRUNTIME_DIR)
            break()
        endif()
    endforeach()

    if(ONNXRUNTIME_DIR)
        # Framework 路径
        if(EXISTS "${ONNXRUNTIME_DIR}/onnxruntime.framework")
            set(ONNXRUNTIME_FRAMEWORK "${ONNXRUNTIME_DIR}/onnxruntime.framework")
            set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_FRAMEWORK}/Headers")
            set(ONNXRUNTIME_LIBRARIES "${ONNXRUNTIME_FRAMEWORK}")
        else()
            set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_DIR}/include")
            set(ONNXRUNTIME_LIBRARIES "${ONNXRUNTIME_DIR}/lib/libonnxruntime.a")
            set(ONNXRUNTIME_IS_STATIC ON)
        endif()
    endif()

elseif(WIN32)
    # ---------- Windows ----------
    set(ONNXRUNTIME_IS_STATIC OFF)

    # 检测架构
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ONNXRUNTIME_ARCH "x64")
    else()
        set(ONNXRUNTIME_ARCH "x86")
    endif()

    # GPU 还是 CPU
    if(ONNXRUNTIME_USE_GPU)
        set(ONNXRUNTIME_VARIANT "gpu")
    else()
        set(ONNXRUNTIME_VARIANT "cpu")
    endif()

    # 搜索路径
    # 注: x64 CPU 优先用 MT (静态 CRT) 版 (onnxruntime-win-x64-MT-Release-*),
    # 微软官方 MD 版 (动态 CRT) 在 plugins/ 下 DllMain 初始化失败 (err=1114)。
    set(ONNXRUNTIME_DIR_NAMES
        "windows/onnxruntime/onnxruntime-win-${ONNXRUNTIME_ARCH}-MT-Release-${ONNXRUNTIME_VERSION}"
        "windows/onnxruntime/onnxruntime-win-${ONNXRUNTIME_ARCH}-${ONNXRUNTIME_VERSION}"
        "windows/onnxruntime/onnxruntime-win-${ONNXRUNTIME_ARCH}-gpu-${ONNXRUNTIME_VERSION}"
        "windows/onnxruntime/onnxruntime-win-${ONNXRUNTIME_ARCH}"
    )

    foreach(dir_name ${ONNXRUNTIME_DIR_NAMES})
        foreach(search_path ${ONNXRUNTIME_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(ONNXRUNTIME_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(ONNXRUNTIME_DIR)
            break()
        endif()
    endforeach()

    if(ONNXRUNTIME_DIR)
        set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_DIR}/include")
        set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_DIR}/lib")

        # 查找库文件
        find_library(ONNXRUNTIME_LIBRARY
            NAMES onnxruntime
            PATHS ${ONNXRUNTIME_LIB_DIR}
            NO_DEFAULT_PATH
        )

        if(ONNXRUNTIME_LIBRARY)
            set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIBRARY})
        endif()

        # DLL 文件
        if(EXISTS "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll")
            list(APPEND ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll")
        endif()
    endif()

elseif(UNIX)
    # ---------- Linux ----------
    set(ONNXRUNTIME_IS_STATIC OFF)

    # 搜索路径
    set(ONNXRUNTIME_DIR_NAMES
        "linux/onnxruntime/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}"
        "linux/onnxruntime/onnxruntime-linux-x64-gpu-${ONNXRUNTIME_VERSION}"
        "linux/onnxruntime"
    )

    foreach(dir_name ${ONNXRUNTIME_DIR_NAMES})
        foreach(search_path ${ONNXRUNTIME_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(ONNXRUNTIME_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(ONNXRUNTIME_DIR)
            break()
        endif()
    endforeach()

    if(ONNXRUNTIME_DIR)
        set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_DIR}/include")
        set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_DIR}/lib")

        # 查找库文件
        find_library(ONNXRUNTIME_LIBRARY
            NAMES onnxruntime
            PATHS ${ONNXRUNTIME_LIB_DIR}
            NO_DEFAULT_PATH
        )

        if(ONNXRUNTIME_LIBRARY)
            set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIBRARY})
        endif()
    endif()
endif()

# 验证
find_package_handle_standard_args(ONNX
    REQUIRED_VARS ONNXRUNTIME_LIBRARIES ONNXRUNTIME_INCLUDE_DIRS
)

# 输出信息
if(ONNX_FOUND)
    message(STATUS "ONNX Runtime found:")
    message(STATUS "  Version: ${ONNXRUNTIME_VERSION}")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIRS}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIBRARIES}")
    message(STATUS "  Static:  ${ONNXRUNTIME_IS_STATIC}")
    if(ONNXRUNTIME_DLLS)
        message(STATUS "  DLLs:    ${ONNXRUNTIME_DLLS}")
    endif()
else()
    message(STATUS "ONNX Runtime not found")
    message(STATUS "  Run download script:")
    if(ANDROID)
        message(STATUS "    python script/onnx/down_onnxruntime_android.py")
    elseif(IOS)
        message(STATUS "    python script/onnx/down_onnxruntime_ios.py")
    elseif(WIN32)
        message(STATUS "    python script/onnx/down_onnxruntime_windows.py")
    elseif(UNIX)
        message(STATUS "    python script/onnx/down_onnxruntime_linux.py")
    endif()
endif()

# 创建导入目标
if(ONNX_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
    if(IOS AND ONNXRUNTIME_FRAMEWORK)
        # iOS Framework
        add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            FRAMEWORK TRUE
            IMPORTED_LOCATION "${ONNXRUNTIME_FRAMEWORK}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIRS}"
        )
    elseif(ONNXRUNTIME_IS_STATIC)
        # 静态库
        add_library(ONNXRuntime::ONNXRuntime STATIC IMPORTED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIRS}"
        )
    else()
        # 动态库
        add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARIES}"
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIRS}"
        )
    endif()
endif()

mark_as_advanced(ONNXRUNTIME_INCLUDE_DIRS ONNXRUNTIME_LIBRARIES ONNXRUNTIME_DLLS)
