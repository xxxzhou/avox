# FindOpenCV.cmake
# 查找 OpenCV 库
#
# 用法:
#   find_package(OpenCV [REQUIRED])
#
# 定义变量:
#   OpenCV_FOUND           - 是否找到
#   OpenCV_INCLUDE_DIRS    - 头文件目录
#   OpenCV_LIBRARIES       - 库文件
#   OpenCV_DLLS            - DLL 文件 (Windows)
#   OpenCV_VERSION         - 版本号
#
# 环境变量:
#   OpenCV_DIR - 自定义 OpenCV 安装路径

include(FindPackageHandleStandardArgs)

# 版本
set(OpenCV_VERSION "4.13.0")

# 默认搜索路径 (avc_library 与项目内 3rdparty/library 目录结构一致: windows/opencv, android/opencv ...)
set(OpenCV_SEARCH_PATHS
    $ENV{OpenCV_DIR}
    ${PROJECT_SOURCE_DIR}/3rdparty/library
    ${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library
)

# ============== 平台检测 ==============

if(ANDROID)
    # ---------- Android ----------
    # Android 使用 opencv_world 静态库

    # 检测架构
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(OpenCV_ARCH "arm64-v8a")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(OpenCV_ARCH "armeabi-v7a")
    else()
        message(WARNING "OpenCV: 不支持的 Android 架构 ${ANDROID_ABI}")
    endif()

    # 搜索路径
    set(OpenCV_DIR_NAMES
        "android/opencv/opencv-${OpenCV_VERSION}-android-${OpenCV_ARCH}"
        "android/opencv"
    )

    foreach(dir_name ${OpenCV_DIR_NAMES})
        foreach(search_path ${OpenCV_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(OpenCV_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(OpenCV_DIR)
            break()
        endif()
    endforeach()

    if(OpenCV_DIR)
        set(OpenCV_INCLUDE_DIRS "${OpenCV_DIR}/include")
        set(OpenCV_LIB_DIR "${OpenCV_DIR}/lib")

        # 查找静态库
        find_library(OpenCV_LIBRARY
            NAMES opencv_world
            PATHS ${OpenCV_LIB_DIR}
            NO_DEFAULT_PATH
        )

        if(OpenCV_LIBRARY)
            set(OpenCV_LIBRARIES ${OpenCV_LIBRARY})
        endif()
    endif()

elseif(IOS)
    # ---------- iOS ----------
    # iOS 使用 framework
    # 注意: iOS OpenCV 需要使用自定义构建或 opencv2.framework

    # 检测架构
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(OpenCV_ARCH "arm64")
    else()
        set(OpenCV_ARCH "x64")
    endif()

    # 搜索路径
    set(OpenCV_DIR_NAMES
        "ios/opencv/opencv2.framework"
        "ios/opencv"
    )

    foreach(dir_name ${OpenCV_DIR_NAMES})
        foreach(search_path ${OpenCV_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(OpenCV_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(OpenCV_DIR)
            break()
        endif()
    endforeach()

    if(OpenCV_DIR)
        # Framework 路径
        if(EXISTS "${OpenCV_DIR}/opencv2.framework")
            set(OpenCV_FRAMEWORK "${OpenCV_DIR}/opencv2.framework")
            set(OpenCV_INCLUDE_DIRS "${OpenCV_FRAMEWORK}/Headers")
            set(OpenCV_LIBRARIES "${OpenCV_FRAMEWORK}")
        else()
            set(OpenCV_INCLUDE_DIRS "${OpenCV_DIR}/include")
            set(OpenCV_LIB_DIR "${OpenCV_DIR}/lib")
            find_library(OpenCV_LIBRARY
                NAMES opencv2
                PATHS ${OpenCV_LIB_DIR}
                NO_DEFAULT_PATH
            )
            if(OpenCV_LIBRARY)
                set(OpenCV_LIBRARIES ${OpenCV_LIBRARY})
            endif()
        endif()
    endif()

elseif(WIN32)
    # ---------- Windows ----------
    # Windows 使用 opencv_world DLL 版本

    # 检测架构
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OpenCV_ARCH "x64")
    else()
        set(OpenCV_ARCH "x86")
    endif()

    # 搜索路径 - OpenCV 下载后会在 windows/opencv/ 下
    # 实际目录结构: opencv-4.13.0-windows/build/x64/vc16/
    set(OpenCV_DIR_NAMES
        "windows/opencv/opencv-${OpenCV_VERSION}-windows"
        "windows/opencv"
    )

    foreach(dir_name ${OpenCV_DIR_NAMES})
        foreach(search_path ${OpenCV_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(OpenCV_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(OpenCV_DIR)
            break()
        endif()
    endforeach()

    if(OpenCV_DIR)
        # 查找 build 目录 - OpenCV 4.13.0 结构: build/x64/vc16/
        set(OpenCV_BUILD_DIR "")
        foreach(sub_dir "${OpenCV_DIR}/build/x64/vc16" "${OpenCV_DIR}/build" "${OpenCV_DIR}/x64/vc16")
            if(EXISTS "${sub_dir}/bin" AND EXISTS "${sub_dir}/lib")
                set(OpenCV_BUILD_DIR ${sub_dir})
                break()
            endif()
        endforeach()

        if(OpenCV_BUILD_DIR)
            set(OpenCV_INCLUDE_DIRS "${OpenCV_DIR}/build/include")
            set(OpenCV_LIB_DIR "${OpenCV_BUILD_DIR}/lib")
            set(OpenCV_BIN_DIR "${OpenCV_BUILD_DIR}/bin")

            # 查找库文件 (opencv_world4130 / opencv_world4130d)
            find_library(OpenCV_LIBRARY_RELEASE
                NAMES opencv_world4130 opencv_world
                PATHS ${OpenCV_LIB_DIR}
                NO_DEFAULT_PATH
            )
            find_library(OpenCV_LIBRARY_DEBUG
                NAMES opencv_world4130d opencv_worldd
                PATHS ${OpenCV_LIB_DIR}
                NO_DEFAULT_PATH
            )

            # 配置库（optimized/debug 关键字形式: 多配置生成器(MSVC)每配置各链对应版本。
            # 不能单值写死 —— Release 链到 debug 库时, 其符号挂 cv::debug_build_guard
            # 命名空间, 与 NDEBUG 编译的代码对不上, LNK2019 cvtColor/imwrite 等）
            if(OpenCV_LIBRARY_RELEASE AND OpenCV_LIBRARY_DEBUG)
                set(OpenCV_LIBRARIES optimized ${OpenCV_LIBRARY_RELEASE} debug ${OpenCV_LIBRARY_DEBUG})
            elseif(OpenCV_LIBRARY_RELEASE)
                set(OpenCV_LIBRARIES ${OpenCV_LIBRARY_RELEASE})
            elseif(OpenCV_LIBRARY_DEBUG)
                set(OpenCV_LIBRARIES ${OpenCV_LIBRARY_DEBUG})
            endif()
            Message(STATUS "OpenCV_LIBRARIES:${OpenCV_LIBRARIES}")

            # DLL 文件（分开 Release 和 Debug）
            if(EXISTS "${OpenCV_BIN_DIR}")
                # Release DLL
                file(GLOB OpenCV_DLL_RELEASE "${OpenCV_BIN_DIR}/opencv_world4130.dll" "${OpenCV_BIN_DIR}/opencv_world.dll")
                # Debug DLL
                file(GLOB OpenCV_DLL_DEBUG "${OpenCV_BIN_DIR}/opencv_world4130d.dll" "${OpenCV_BIN_DIR}/opencv_worldd.dll")

                if(OpenCV_DLL_RELEASE)
                    set(OpenCV_DLLS_RELEASE ${OpenCV_DLL_RELEASE})
                endif()
                if(OpenCV_DLL_DEBUG)
                    set(OpenCV_DLLS_DEBUG ${OpenCV_DLL_DEBUG})
                endif()
            endif()
        endif()
    endif()

elseif(UNIX AND NOT APPLE)
    # ---------- Linux ----------
    # Linux 使用预编译包或源码构建

    # 搜索路径
    set(OpenCV_DIR_NAMES
        "linux/opencv/opencv-${OpenCV_VERSION}-linux-x64"
        "linux/opencv"
    )

    foreach(dir_name ${OpenCV_DIR_NAMES})
        foreach(search_path ${OpenCV_SEARCH_PATHS})
            if(EXISTS "${search_path}/${dir_name}")
                set(OpenCV_DIR "${search_path}/${dir_name}")
                break()
            endif()
        endforeach()
        if(OpenCV_DIR)
            break()
        endif()
    endforeach()

    if(OpenCV_DIR)
        # 查找 build 目录
        set(OpenCV_BUILD_DIR "")
        foreach(sub_dir ${OpenCV_DIR} "${OpenCV_DIR}/build")
            if(EXISTS "${sub_dir}/lib" AND EXISTS "${sub_dir}/include")
                set(OpenCV_BUILD_DIR ${sub_dir})
                break()
            endif()
        endforeach()

        if(OpenCV_BUILD_DIR)
            set(OpenCV_INCLUDE_DIRS "${OpenCV_BUILD_DIR}/include")
            set(OpenCV_LIB_DIR "${OpenCV_BUILD_DIR}/lib")

            # 查找库文件
            find_library(OpenCV_LIBRARY
                NAMES opencv_world
                PATHS ${OpenCV_LIB_DIR}
                NO_DEFAULT_PATH
            )

            if(OpenCV_LIBRARY)
                set(OpenCV_LIBRARIES ${OpenCV_LIBRARY})
            endif()
        endif()
    endif()

    # 如果没找到，尝试使用系统 OpenCV (使用 CMake 自带的查找模块)
    if(NOT OpenCV_FOUND)
        # 临时移除当前模块路径，避免递归调用
        set(_TEMP_MODULE_PATH ${CMAKE_MODULE_PATH})
        set(CMAKE_MODULE_PATH "")
        find_package(OpenCV QUIET PATHS /usr/local/lib/cmake/opencv4 /usr/lib/cmake/opencv4)
        set(CMAKE_MODULE_PATH ${_TEMP_MODULE_PATH})
        if(OpenCV_FOUND)
            message(STATUS "Using system OpenCV: ${OpenCV_VERSION}")
        endif()
    endif()
endif()

# 验证
find_package_handle_standard_args(OpenCV
    REQUIRED_VARS OpenCV_LIBRARIES OpenCV_INCLUDE_DIRS
    VERSION_VAR OpenCV_VERSION
)

# 输出信息
if(OpenCV_FOUND)
    message(STATUS "OpenCV found:")
    message(STATUS "  Version: ${OpenCV_VERSION}")
    message(STATUS "  Include: ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "  Library: ${OpenCV_LIBRARIES}")
    if(OpenCV_DLLS)
        message(STATUS "  DLLs:    ${OpenCV_DLLS}")
    endif()
    if(OpenCV_FRAMEWORK)
        message(STATUS "  Framework: ${OpenCV_FRAMEWORK}")
    endif()
else()
    message(STATUS "OpenCV not found")
    message(STATUS "  Run download script:")
    if(ANDROID)
        message(STATUS "    python script/opencv/down_opencv_android.py")
    elseif(IOS)
        message(STATUS "    python script/opencv/down_opencv_ios.py")
    elseif(WIN32)
        message(STATUS "    python script/opencv/down_opencv_windows.py")
    elseif(UNIX AND NOT APPLE)
        message(STATUS "    python script/opencv/down_opencv_linux.py")
    endif()
endif()

# 创建导入目标
if(OpenCV_FOUND AND NOT TARGET OpenCV::OpenCV)
    if(IOS AND OpenCV_FRAMEWORK)
        # iOS Framework
        add_library(OpenCV::OpenCV SHARED IMPORTED)
        set_target_properties(OpenCV::OpenCV PROPERTIES
            FRAMEWORK TRUE
            IMPORTED_LOCATION "${OpenCV_FRAMEWORK}"
            INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}"
        )
    elseif(WIN32)
        # Windows DLL with import library
        # OpenCV_LIBRARIES 是 optimized/debug 关键字列表, 导入目标按配置各取对应 implib
        add_library(OpenCV::OpenCV SHARED IMPORTED)
        set_target_properties(OpenCV::OpenCV PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}"
        )
        if(OpenCV_LIBRARY_RELEASE)
            set_target_properties(OpenCV::OpenCV PROPERTIES
                IMPORTED_IMPLIB_RELEASE "${OpenCV_LIBRARY_RELEASE}")
        endif()
        if(OpenCV_LIBRARY_DEBUG)
            set_target_properties(OpenCV::OpenCV PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${OpenCV_LIBRARY_DEBUG}"
                MAP_IMPORTED_CONFIG_MINSIZEREL Release
                MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
        endif()
    else()
        # Static library or Linux
        add_library(OpenCV::OpenCV SHARED IMPORTED)
        set_target_properties(OpenCV::OpenCV PROPERTIES
            IMPORTED_LOCATION "${OpenCV_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}"  
        )
    endif()
endif()

mark_as_advanced(OpenCV_INCLUDE_DIRS OpenCV_LIBRARIES OpenCV_DIR)

# 不再 avox_run_module_copy 到顶层 —— opencv_world4xx.dll 只随 avox_opencv 插件进 plugins/
# (核心已解链 opencv, 无顶层依赖)。OpenCV_DLLS_RELEASE/DEBUG 变量仍保留(上方 :199-204),
# 供 plugins/avox_opencv/CMakeLists.txt 作 DEP_DLLS 拷进 plugins/ 使其自包含(与 onnx 同)。