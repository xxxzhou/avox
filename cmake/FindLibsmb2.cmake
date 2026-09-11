# FindLibsmb2.cmake
# 查找 libsmb2 预编译产物(avox_remote 插件 "smb" 源依赖)
#
# 源项目: D:\Work\github\libsmb2 (python build_windows.py 编译并安装到库仓)
# 用法:
#   find_package(Libsmb2 QUIET)
#
# 定义变量:
#   Libsmb2_FOUND           - 是否找到
#   LIBSMB2_INCLUDE_DIRS    - 头文件目录(libsmb2 安装的 include/)
#   LIBSMB2_LIBRARIES       - 库文件(smb2 静态库)
#   Libsmb2_VERSION         - 版本号
#
# 环境变量:
#   LIBSMB2_DIR - 自定义 libsmb2 安装路径

include(FindPackageHandleStandardArgs)

set(Libsmb2_VERSION "6.2")

# 默认搜索路径: 库仓(library/<platform>/libsmb2) 与 工程内3rdparty 兜底
set(Libsmb2_SEARCH_PATHS
    $ENV{LIBSMB2_DIR}
    ${PROJECT_SOURCE_DIR}/../avc_library/3rdparty/library
    ${PROJECT_SOURCE_DIR}/3rdparty/library
)

# ============== 平台检测 ==============
if(ANDROID)
    # Android: <abi>/ 子目录布局(库仓 android/libsmb2/<abi>/{include,lib})
    set(Libsmb2_PLATFORM_DIR_NAMES "android/libsmb2/${ANDROID_ABI}")
elseif(WIN32)
    set(Libsmb2_PLATFORM_DIR_NAMES "windows/libsmb2")
elseif(IOS OR APPLE)
    set(Libsmb2_PLATFORM_DIR_NAMES "ios/libsmb2" "mac/libsmb2")
else()
    set(Libsmb2_PLATFORM_DIR_NAMES "linux/libsmb2")
endif()

set(Libsmb2_DIR "")
foreach(dir_name ${Libsmb2_PLATFORM_DIR_NAMES})
    foreach(search_path ${Libsmb2_SEARCH_PATHS})
        if(EXISTS "${search_path}/${dir_name}/include/smb2/libsmb2.h")
            set(Libsmb2_DIR "${search_path}/${dir_name}")
            break()
        endif()
    endforeach()
    if(Libsmb2_DIR)
        break()
    endif()
endforeach()

if(Libsmb2_DIR)
    set(LIBSMB2_INCLUDE_DIRS "${Libsmb2_DIR}/include")
    set(Libsmb2_LIB_DIR "${Libsmb2_DIR}/lib")

    if(WIN32)
        # 静态库 release/debug 双配置(opencv 同款 optimized/debug 关键字形式)
        find_library(Libsmb2_LIBRARY_RELEASE
            NAMES smb2
            PATHS ${Libsmb2_LIB_DIR}
            NO_DEFAULT_PATH)
        find_library(Libsmb2_LIBRARY_DEBUG
            NAMES smb2_d smb2d
            PATHS ${Libsmb2_LIB_DIR}
            NO_DEFAULT_PATH)
        if(Libsmb2_LIBRARY_RELEASE AND Libsmb2_LIBRARY_DEBUG)
            set(LIBSMB2_LIBRARIES optimized ${Libsmb2_LIBRARY_RELEASE}
                debug ${Libsmb2_LIBRARY_DEBUG})
        elseif(Libsmb2_LIBRARY_RELEASE)
            set(LIBSMB2_LIBRARIES ${Libsmb2_LIBRARY_RELEASE})
        elseif(Libsmb2_LIBRARY_DEBUG)
            set(LIBSMB2_LIBRARIES ${Libsmb2_LIBRARY_DEBUG})
        endif()
    else()
        find_library(Libsmb2_LIBRARY
            NAMES smb2
            PATHS ${Libsmb2_LIB_DIR}
            NO_DEFAULT_PATH)
        if(Libsmb2_LIBRARY)
            set(LIBSMB2_LIBRARIES ${Libsmb2_LIBRARY})
        endif()
    endif()

    # Windows 系统库(libsmb2 直连 winsock)
    if(WIN32)
        list(APPEND LIBSMB2_LIBRARIES ws2_32)
    endif()
endif()

find_package_handle_standard_args(Libsmb2
    REQUIRED_VARS LIBSMB2_LIBRARIES LIBSMB2_INCLUDE_DIRS
    VERSION_VAR Libsmb2_VERSION
)

if(Libsmb2_FOUND)
    message(STATUS "libsmb2 found:")
    message(STATUS "  Version: ${Libsmb2_VERSION}")
    message(STATUS "  Include: ${LIBSMB2_INCLUDE_DIRS}")
    message(STATUS "  Library: ${LIBSMB2_LIBRARIES}")
else()
    message(STATUS "libsmb2 not found")
    message(STATUS "  在 D:/Work/github/libsmb2 下执行 python build_windows.py 编译安装")
endif()

mark_as_advanced(LIBSMB2_INCLUDE_DIRS LIBSMB2_LIBRARIES Libsmb2_DIR)
