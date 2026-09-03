# FindLibtorrent.cmake
# 查找 libtorrent 预编译产物(avox_torrent 插件依赖)
#
# 源项目: D:\Work\github\libtorrent (python build_windows.py 编译并安装到库仓)
# 用法:
#   find_package(Libtorrent [REQUIRED])
#
# 定义变量:
#   Libtorrent_FOUND              - 是否找到
#   LIBTORRENT_INCLUDE_DIRS       - 头文件目录(libtorrent 安装的 include/)
#   LIBTORRENT_LIBRARIES          - 库文件(torrent-rasterbar 静态库)
#   Libtorrent_BOOST_DIR          - 配套 Boost 头文件目录(编译消费者代码必需)
#   Libtorrent_VERSION            - 版本号
#
# 环境变量:
#   LIBTORRENT_DIR - 自定义 libtorrent 安装路径

include(FindPackageHandleStandardArgs)

set(Libtorrent_VERSION "2.0.14")

# 默认搜索路径: 库仓(library/<platform>/libtorrent) 与 工程内3rdparty 兜底
set(Libtorrent_SEARCH_PATHS
    $ENV{LIBTORRENT_DIR}
    ${PROJECT_SOURCE_DIR}/../avc_library/3rdparty/library
    ${PROJECT_SOURCE_DIR}/3rdparty/library
)

# ============== 平台检测 ==============
if(ANDROID)
    # Android: <abi>/ 子目录布局(库仓 android/libtorrent/<abi>/{include,lib})
    set(Libtorrent_PLATFORM_DIR_NAMES "android/libtorrent/${ANDROID_ABI}")
elseif(WIN32)
    set(Libtorrent_PLATFORM_DIR_NAMES "windows/libtorrent")
elseif(IOS OR APPLE)
    set(Libtorrent_PLATFORM_DIR_NAMES "ios/libtorrent" "mac/libtorrent")
else()
    set(Libtorrent_PLATFORM_DIR_NAMES "linux/libtorrent")
endif()

set(Libtorrent_DIR "")
foreach(dir_name ${Libtorrent_PLATFORM_DIR_NAMES})
    foreach(search_path ${Libtorrent_SEARCH_PATHS})
        if(EXISTS "${search_path}/${dir_name}/include/libtorrent/session.hpp")
            set(Libtorrent_DIR "${search_path}/${dir_name}")
            break()
        endif()
    endforeach()
    if(Libtorrent_DIR)
        break()
    endif()
endforeach()

if(Libtorrent_DIR)
    set(LIBTORRENT_INCLUDE_DIRS "${Libtorrent_DIR}/include")
    set(Libtorrent_LIB_DIR "${Libtorrent_DIR}/lib")

    if(WIN32)
        # 静态库 release/debug 双配置(opencv 同款 optimized/debug 关键字形式)
        find_library(Libtorrent_LIBRARY_RELEASE
            NAMES torrent-rasterbar
            PATHS ${Libtorrent_LIB_DIR}
            NO_DEFAULT_PATH)
        find_library(Libtorrent_LIBRARY_DEBUG
            NAMES torrent-rasterbar_d torrent-rasterbard
            PATHS ${Libtorrent_LIB_DIR}
            NO_DEFAULT_PATH)
        if(Libtorrent_LIBRARY_RELEASE AND Libtorrent_LIBRARY_DEBUG)
            set(LIBTORRENT_LIBRARIES optimized ${Libtorrent_LIBRARY_RELEASE}
                debug ${Libtorrent_LIBRARY_DEBUG})
        elseif(Libtorrent_LIBRARY_RELEASE)
            set(LIBTORRENT_LIBRARIES ${Libtorrent_LIBRARY_RELEASE})
        elseif(Libtorrent_LIBRARY_DEBUG)
            set(LIBTORRENT_LIBRARIES ${Libtorrent_LIBRARY_DEBUG})
        endif()
    else()
        find_library(Libtorrent_LIBRARY
            NAMES torrent-rasterbar
            PATHS ${Libtorrent_LIB_DIR}
            NO_DEFAULT_PATH)
        if(Libtorrent_LIBRARY)
            set(LIBTORRENT_LIBRARIES ${Libtorrent_LIBRARY})
        endif()
    endif()
endif()

# 配套 Boost 头文件(libtorrent 安装头文件里 #include boost/*, 消费者编译需要):
# 独立放 github 工作区根: <avplay>/../boost (即 D:\Work\github\boost),
# 或用环境变量 BOOST_ROOT 指定
if(Libtorrent_DIR)
    foreach(_lt_boost_cand
            "$ENV{BOOST_ROOT}"
            "${PROJECT_SOURCE_DIR}/../boost")
        if(EXISTS "${_lt_boost_cand}/boost/version.hpp")
            set(Libtorrent_BOOST_DIR "${_lt_boost_cand}")
            break()
        endif()
    endforeach()
endif()

find_package_handle_standard_args(Libtorrent
    REQUIRED_VARS LIBTORRENT_LIBRARIES LIBTORRENT_INCLUDE_DIRS
    VERSION_VAR Libtorrent_VERSION
)

if(Libtorrent_FOUND)
    message(STATUS "libtorrent found:")
    message(STATUS "  Version: ${Libtorrent_VERSION}")
    message(STATUS "  Include: ${LIBTORRENT_INCLUDE_DIRS}")
    message(STATUS "  Library: ${LIBTORRENT_LIBRARIES}")
    message(STATUS "  Boost:   ${Libtorrent_BOOST_DIR}")
else()
    message(STATUS "libtorrent not found")
    message(STATUS "  在 D:/Work/github/libtorrent 下执行 python build_windows.py 编译安装")
endif()

mark_as_advanced(LIBTORRENT_INCLUDE_DIRS LIBTORRENT_LIBRARIES Libtorrent_DIR
    Libtorrent_BOOST_DIR)
