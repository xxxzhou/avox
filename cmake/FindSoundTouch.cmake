# FindSoundTouch.cmake
# 查找 soundtouch 预编译产物(avox_tempo 插件音频变速不变调依赖)
#
# 源项目: D:\Work\github\soundtouch (python build_windows.py 编译并安装到库仓)
# 用法:
#   find_package(SoundTouch QUIET)
#
# 定义变量:
#   SoundTouch_FOUND          - 是否找到
#   SOUNDTOUCH_INCLUDE_DIRS   - 头文件目录(soundtouch 安装的 include/)
#   SOUNDTOUCH_LIBRARIES      - 库文件(SoundTouch 静态库)
#   SoundTouch_VERSION        - 版本号
#
# 环境变量:
#   SOUNDTOUCH_DIR - 自定义 soundtouch 安装路径

include(FindPackageHandleStandardArgs)

set(SoundTouch_VERSION "2.4.1")

# 默认搜索路径: 库仓(library/<platform>/soundtouch) 与 工程内3rdparty 兜底
set(SoundTouch_SEARCH_PATHS
    $ENV{SOUNDTOUCH_DIR}
    ${PROJECT_SOURCE_DIR}/../avc_library/3rdparty/library
    ${PROJECT_SOURCE_DIR}/3rdparty/library
)

# ============== 平台检测 ==============
if(ANDROID)
    # Android: <abi>/ 子目录布局(库仓 android/soundtouch/<abi>/{include,lib})
    set(SoundTouch_PLATFORM_DIR_NAMES "android/soundtouch/${ANDROID_ABI}")
elseif(WIN32)
    set(SoundTouch_PLATFORM_DIR_NAMES "windows/soundtouch")
elseif(CMAKE_SYSTEM_NAME MATCHES "iOS")
    set(SoundTouch_PLATFORM_DIR_NAMES "ios/soundtouch")
elseif(APPLE)
    # macOS 必须与 iOS 分开: ios 的 arm64 .a 在 macOS 链接报 "built for iOS";
    # 库仓目录名与 ffmpeg/ass 静态库一致用 darwin(历史名 mac/ 留作兼容)
    set(SoundTouch_PLATFORM_DIR_NAMES "darwin/soundtouch" "mac/soundtouch")
else()
    set(SoundTouch_PLATFORM_DIR_NAMES "linux/soundtouch")
endif()

set(SoundTouch_DIR "")
foreach(dir_name ${SoundTouch_PLATFORM_DIR_NAMES})
    foreach(search_path ${SoundTouch_SEARCH_PATHS})
        if(EXISTS "${search_path}/${dir_name}/include/soundtouch/SoundTouch.h")
            set(SoundTouch_DIR "${search_path}/${dir_name}")
            break()
        endif()
    endforeach()
    if(SoundTouch_DIR)
        break()
    endif()
endforeach()

if(SoundTouch_DIR)
    set(SOUNDTOUCH_INCLUDE_DIRS "${SoundTouch_DIR}/include")
    set(SoundTouch_LIB_DIR "${SoundTouch_DIR}/lib")

    if(WIN32)
        # 静态库 release/debug 双配置(libsmb2 同款 optimized/debug 关键字形式)
        find_library(SoundTouch_LIBRARY_RELEASE
            NAMES SoundTouch
            PATHS ${SoundTouch_LIB_DIR}
            NO_DEFAULT_PATH)
        find_library(SoundTouch_LIBRARY_DEBUG
            NAMES SoundTouch_d SoundTouchd
            PATHS ${SoundTouch_LIB_DIR}
            NO_DEFAULT_PATH)
        if(SoundTouch_LIBRARY_RELEASE AND SoundTouch_LIBRARY_DEBUG)
            set(SOUNDTOUCH_LIBRARIES optimized ${SoundTouch_LIBRARY_RELEASE}
                debug ${SoundTouch_LIBRARY_DEBUG})
        elseif(SoundTouch_LIBRARY_RELEASE)
            set(SOUNDTOUCH_LIBRARIES ${SoundTouch_LIBRARY_RELEASE})
        elseif(SoundTouch_LIBRARY_DEBUG)
            set(SOUNDTOUCH_LIBRARIES ${SoundTouch_LIBRARY_DEBUG})
        endif()
    else()
        find_library(SoundTouch_LIBRARY
            NAMES SoundTouch
            PATHS ${SoundTouch_LIB_DIR}
            NO_DEFAULT_PATH)
        if(SoundTouch_LIBRARY)
            set(SOUNDTOUCH_LIBRARIES ${SoundTouch_LIBRARY})
        endif()
    endif()
endif()

find_package_handle_standard_args(SoundTouch
    REQUIRED_VARS SOUNDTOUCH_LIBRARIES SOUNDTOUCH_INCLUDE_DIRS
    VERSION_VAR SoundTouch_VERSION
)

if(SoundTouch_FOUND)
    message(STATUS "soundtouch found:")
    message(STATUS "  Version: ${SoundTouch_VERSION}")
    message(STATUS "  Include: ${SOUNDTOUCH_INCLUDE_DIRS}")
    message(STATUS "  Library: ${SOUNDTOUCH_LIBRARIES}")
else()
    message(STATUS "soundtouch not found")
    message(STATUS "  在 D:/Work/github/soundtouch 下执行 python build_windows.py 编译安装")
endif()

mark_as_advanced(SOUNDTOUCH_INCLUDE_DIRS SOUNDTOUCH_LIBRARIES SoundTouch_DIR)
