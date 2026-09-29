# FindLibbluray.cmake
# 查找 libbluray 预编译产物(avox_disc 插件依赖)
#
# 源项目: D:\Work\github\libbluray (MSYS2 autotools --disable-bd-j 编译并安装到库仓,
#         四步配方见 plugins/avox_disc/REBUILD.md)
# 用法:
#   find_package(Libbluray [REQUIRED])
#
# 定义变量:
#   Libbluray_FOUND          - 是否找到
#   LIBBLURAY_INCLUDE_DIRS   - 头文件目录(include/)
#   LIBBLURAY_LIBRARIES      - 导入库(MSVC .lib, 由 make_msvc_lib.py 从 dll 生成)
#   LIBBLURAY_BIN_DLL        - 运行期 dll 路径(register_plugin DEP_DLLS 拷进 plugins/)
#   Libbluray_VERSION        - 版本号
#
# 环境变量:
#   LIBBLURAY_DIR - 自定义 libbluray 安装路径

include(FindPackageHandleStandardArgs)

set(Libbluray_VERSION "1.3.4")

# 默认搜索路径: 库仓(library/<platform>/libbluray) 与 工程内3rdparty 兜底
set(Libbluray_SEARCH_PATHS
    $ENV{LIBBLURAY_DIR}
    ${PROJECT_SOURCE_DIR}/../avox_library/3rdparty/library
    ${PROJECT_SOURCE_DIR}/3rdparty/library
)

# ============== 平台检测 ==============
if(ANDROID)
    set(Libbluray_PLATFORM_DIR_NAMES "android/libbluray/${ANDROID_ABI}")
elseif(WIN32)
    set(Libbluray_PLATFORM_DIR_NAMES "windows/libbluray")
elseif(IOS)
    set(Libbluray_PLATFORM_DIR_NAMES "ios/libbluray" "darwin/libbluray")
elseif(APPLE)
    set(Libbluray_PLATFORM_DIR_NAMES "darwin/libbluray" "mac/libbluray")
else()
    set(Libbluray_PLATFORM_DIR_NAMES "linux/libbluray")
endif()

set(Libbluray_DIR "")
foreach(dir_name ${Libbluray_PLATFORM_DIR_NAMES})
    foreach(search_path ${Libbluray_SEARCH_PATHS})
        if(EXISTS "${search_path}/${dir_name}/include/libbluray/bluray.h")
            set(Libbluray_DIR "${search_path}/${dir_name}")
            break()
        endif()
    endforeach()
    if(Libbluray_DIR)
        break()
    endif()
endforeach()

if(Libbluray_DIR)
    set(LIBBLURAY_INCLUDE_DIRS "${Libbluray_DIR}/include")
    set(Libbluray_LIB_DIR "${Libbluray_DIR}/lib")
    if(WIN32)
        # MSVC 链接需要 make_msvc_lib.py 生成的导入库(.dll.a 是 MinGW 格式)
        find_library(LIBBLURAY_LIBRARIES
            NAMES libbluray bluray
            PATHS ${Libbluray_LIB_DIR}
            NO_DEFAULT_PATH)
    else()
        find_library(LIBBLURAY_LIBRARIES
            NAMES bluray
            PATHS ${Libbluray_LIB_DIR}
            NO_DEFAULT_PATH)
    endif()
    # 运行期 dll(MinGW 构建, 随插件分发)
    if(WIN32)
        find_file(LIBBLURAY_BIN_DLL
            NAMES libbluray-2.dll libbluray.dll
            PATHS "${Libbluray_DIR}/bin"
            NO_DEFAULT_PATH)
    endif()
endif()

find_package_handle_standard_args(Libbluray
    REQUIRED_VARS LIBBLURAY_LIBRARIES LIBBLURAY_INCLUDE_DIRS
    VERSION_VAR Libbluray_VERSION
)

if(Libbluray_FOUND)
    message(STATUS "libbluray found:")
    message(STATUS "  Version: ${Libbluray_VERSION}")
    message(STATUS "  Include: ${LIBBLURAY_INCLUDE_DIRS}")
    message(STATUS "  Library: ${LIBBLURAY_LIBRARIES}")
    message(STATUS "  Dll:     ${LIBBLURAY_BIN_DLL}")
else()
    message(STATUS "libbluray not found")
    message(STATUS "  在 D:/Work/github/libbluray 下按 plugins/avox_disc/REBUILD.md 编译安装")
endif()

mark_as_advanced(LIBBLURAY_INCLUDE_DIRS LIBBLURAY_LIBRARIES LIBBLURAY_BIN_DLL
    Libbluray_DIR)
