# FindOpenSSL.cmake
# 查找 OpenSSL 库
#
# 用法:
#   find_package(OpenSSL [QUIET])
#
# 搜索优先级:
#   1. OPENSSL_ROOT_DIR (CMake 变量或环境变量) — 用户显式指定
#   2. AVOX_EXTERNAL_LIBRARY_DIR/3rdparty/library/{platform}/openssl — 外部库目录
#   3. CMake 内置 FindOpenSSL — 系统安装的 OpenSSL (如 Windows: C:\Program Files\OpenSSL-Win64)
#
# 定义变量:
#   OPENSSL_FOUND         - 是否找到
#   OPENSSL_INCLUDE_DIRS  - 头文件目录
#   OPENSSL_LIBRARIES     - 库文件 (libssl + libcrypto)
#   OPENSSL_DLLS          - DLL 文件 (仅 Windows)
#   OPENSSL_IS_STATIC     - 是否静态库
#   OPENSSL_VERSION       - 版本号

include(FindPackageHandleStandardArgs)

# 初始化输出变量
set(OPENSSL_INCLUDE_DIRS "")
set(OPENSSL_LIBRARIES "")
set(OPENSSL_DLLS "")
set(OPENSSL_IS_STATIC OFF)
set(OPENSSL_VERSION "")

# ========== 辅助: 构建搜索路径 ==========

# 优先使用 OPENSSL_ROOT_DIR (CMake 变量 > 环境变量)
if(NOT OPENSSL_ROOT_DIR)
    if(DEFINED ENV{OPENSSL_ROOT_DIR})
        set(OPENSSL_ROOT_DIR "$ENV{OPENSSL_ROOT_DIR}")
    endif()
endif()

# 构建外部库搜索路径
set(_OPENSSL_EXTERNAL_PATHS "")
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    list(APPEND _OPENSSL_EXTERNAL_PATHS "${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library")
endif()
# 向后兼容
list(APPEND _OPENSSL_EXTERNAL_PATHS "${PROJECT_SOURCE_DIR}/../avc_library/3rdparty/library")
list(APPEND _OPENSSL_EXTERNAL_PATHS "${PROJECT_SOURCE_DIR}/3rdparty/library")

# ========== 平台检测 ==========

if(OPENSSL_ROOT_DIR)
    # 用户显式指定了 OPENSSL_ROOT_DIR，直接使用
    message(STATUS "OpenSSL: using OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")

    if(ANDROID)
        # Android + OPENSSL_ROOT_DIR: 仍需定位 arch 子目录
        set(_OPENSSL_ARCH_DIR "${OPENSSL_ROOT_DIR}/${ANDROID_ABI}")
        if(EXISTS "${_OPENSSL_ARCH_DIR}/libssl.a")
            set(OPENSSL_INCLUDE_DIRS "${OPENSSL_ROOT_DIR}/include")
            set(OPENSSL_LIBRARIES "${_OPENSSL_ARCH_DIR}/libssl.a" "${_OPENSSL_ARCH_DIR}/libcrypto.a")
            set(OPENSSL_IS_STATIC ON)
        else()
            set(OPENSSL_INCLUDE_DIRS "${OPENSSL_ROOT_DIR}/include")
            find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
            find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
            if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
                set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
                set(OPENSSL_IS_STATIC ON)
            endif()
        endif()

    elseif(WIN32)
        # Windows + OPENSSL_ROOT_DIR
        # OpenSSL 官方安装包只提供 Release CRT (/MD) 版 DLL，
        # Debug 构建也必须链接 /MD 版 import library，否则 CRT 不匹配会崩溃。
        set(OPENSSL_INCLUDE_DIRS "${OPENSSL_ROOT_DIR}/include")
        find_library(_OPENSSL_SSL_LIB NAMES ssl ssleay32 PATHS "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD" "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto libeay32 PATHS "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD" "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
        endif()
        # 查找 DLL
        find_file(_OPENSSL_SSL_DLL NAMES libssl-3-x64.dll libssl-1_1-x64.dll PATHS "${OPENSSL_ROOT_DIR}/bin" NO_DEFAULT_PATH)
        find_file(_OPENSSL_CRYPTO_DLL NAMES libcrypto-3-x64.dll libcrypto-1_1-x64.dll PATHS "${OPENSSL_ROOT_DIR}/bin" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_DLL AND _OPENSSL_CRYPTO_DLL)
            list(APPEND OPENSSL_DLLS ${_OPENSSL_SSL_DLL} ${_OPENSSL_CRYPTO_DLL})
        endif()

    elseif(IOS)
        set(OPENSSL_INCLUDE_DIRS "${OPENSSL_ROOT_DIR}/include")
        find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
            set(OPENSSL_IS_STATIC ON)
        endif()

    else()
        # Linux 等
        set(OPENSSL_INCLUDE_DIRS "${OPENSSL_ROOT_DIR}/include")
        find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
        endif()
    endif()

elseif(ANDROID)
    # ========== Android: 从 AVOX_EXTERNAL_LIBRARY_DIR 查找 ==========
    set(_OPENSSL_FOUND_DIR "")

    foreach(_search_path ${_OPENSSL_EXTERNAL_PATHS})
        set(_openssl_dir "${_search_path}/android/openssl")
        if(EXISTS "${_openssl_dir}/include/openssl/ssl.h")
            set(_OPENSSL_FOUND_DIR "${_openssl_dir}")
            break()
        endif()
    endforeach()

    if(_OPENSSL_FOUND_DIR)
        set(OPENSSL_INCLUDE_DIRS "${_OPENSSL_FOUND_DIR}/include")

        # 架构子目录: openssl/arm64-v8a/libssl.a
        set(_OPENSSL_ARCH_DIR "${_OPENSSL_FOUND_DIR}/${ANDROID_ABI}")
        if(EXISTS "${_OPENSSL_ARCH_DIR}/libssl.a")
            set(OPENSSL_LIBRARIES "${_OPENSSL_ARCH_DIR}/libssl.a" "${_OPENSSL_ARCH_DIR}/libcrypto.a")
            set(OPENSSL_IS_STATIC ON)
        else()
            # 回退到 lib/ 子目录
            find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
            find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
            if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
                set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
                set(OPENSSL_IS_STATIC ON)
            endif()
        endif()
    endif()

elseif(WIN32)
    # ========== Windows: 先查外部目录，再查系统安装 ==========

    # 先尝试外部库目录
    set(_OPENSSL_FOUND_DIR "")
    foreach(_search_path ${_OPENSSL_EXTERNAL_PATHS})
        set(_openssl_dir "${_search_path}/windows/openssl")
        if(EXISTS "${_openssl_dir}/include/openssl/ssl.h")
            set(_OPENSSL_FOUND_DIR "${_openssl_dir}")
            break()
        endif()
    endforeach()

    if(_OPENSSL_FOUND_DIR)
        set(OPENSSL_INCLUDE_DIRS "${_OPENSSL_FOUND_DIR}/include")
        find_library(_OPENSSL_SSL_LIB NAMES ssl ssleay32 PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto libeay32 PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
        endif()
        # DLL
        find_file(_OPENSSL_SSL_DLL NAMES libssl-3-x64.dll libssl-1_1-x64.dll PATHS "${_OPENSSL_FOUND_DIR}/bin" NO_DEFAULT_PATH)
        find_file(_OPENSSL_CRYPTO_DLL NAMES libcrypto-3-x64.dll libcrypto-1_1-x64.dll PATHS "${_OPENSSL_FOUND_DIR}/bin" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_DLL AND _OPENSSL_CRYPTO_DLL)
            list(APPEND OPENSSL_DLLS ${_OPENSSL_SSL_DLL} ${_OPENSSL_CRYPTO_DLL})
        endif()
    else()
        # 回退到系统安装的 OpenSSL (如 C:\Program Files\OpenSSL-Win64)
        # 使用 CMake 内置的 FindOpenSSL
        set(_OPENSSL_USE_BUILTIN ON)
    endif()

elseif(IOS)
    # ========== iOS: 从 AVOX_EXTERNAL_LIBRARY_DIR 查找 ==========
    set(_OPENSSL_FOUND_DIR "")

    foreach(_search_path ${_OPENSSL_EXTERNAL_PATHS})
        set(_openssl_dir "${_search_path}/ios/openssl")
        if(EXISTS "${_openssl_dir}/include/openssl/ssl.h")
            set(_OPENSSL_FOUND_DIR "${_openssl_dir}")
            break()
        endif()
    endforeach()

    if(_OPENSSL_FOUND_DIR)
        set(OPENSSL_INCLUDE_DIRS "${_OPENSSL_FOUND_DIR}/include")
        # iOS 可能用 xcframework 或静态库
        find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${_OPENSSL_FOUND_DIR}/lib" "${_OPENSSL_FOUND_DIR}/${CMAKE_BUILD_TYPE}-iphoneos" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${_OPENSSL_FOUND_DIR}/lib" "${_OPENSSL_FOUND_DIR}/${CMAKE_BUILD_TYPE}-iphoneos" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
            set(OPENSSL_IS_STATIC ON)
        endif()
    endif()

elseif(UNIX)
    # ========== Linux: 先查外部目录，再查系统安装 ==========
    set(_OPENSSL_FOUND_DIR "")

    foreach(_search_path ${_OPENSSL_EXTERNAL_PATHS})
        set(_openssl_dir "${_search_path}/linux/openssl")
        if(EXISTS "${_openssl_dir}/include/openssl/ssl.h")
            set(_OPENSSL_FOUND_DIR "${_openssl_dir}")
            break()
        endif()
    endforeach()

    if(_OPENSSL_FOUND_DIR)
        set(OPENSSL_INCLUDE_DIRS "${_OPENSSL_FOUND_DIR}/include")
        find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
        find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${_OPENSSL_FOUND_DIR}/lib" NO_DEFAULT_PATH)
        if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
            set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
        endif()
    else()
        # 回退到系统安装的 OpenSSL
        set(_OPENSSL_USE_BUILTIN ON)
    endif()
endif()

# ========== 回退: 使用 CMake 内置 FindOpenSSL ==========

# 注意: 不能在此文件内递归调用 find_package(OpenSSL)，否则会无限递归。
# 方案: 使用 CMAKE_FIND_PACKAGE_PREFER_CONFIG 选项或直接手动查找。
# 这里采用 _find_package 技巧: 临时将本文件从搜索路径排除，让 CMake 使用内置模块。

if(_OPENSSL_USE_BUILTIN OR (NOT OPENSSL_LIBRARIES AND NOT OPENSSL_ROOT_DIR))
    # Windows: 直接查找系统安装的 OpenSSL，始终使用 /MD 版 import library。
    # OpenSSL 官方安装包只提供 Release CRT (/MD) 版 DLL，Debug 构建也必须链接 /MD 版。
    # 不能使用 include(CMake内置FindOpenSSL) 因为它会设置 CACHE 变量且包含 /MDd 路径。
    if(WIN32)
        # 查找系统安装的 OpenSSL (如 C:\Program Files\OpenSSL-Win64)
        set(_OPENSSL_WIN_SEARCH_PATHS
            "C:/Program Files/OpenSSL-Win64"
            "C:/OpenSSL-Win64"
            "C:/OpenSSL"
            "$ENV{PROGRAMFILES}/OpenSSL-Win64"
        )
        foreach(_ssl_dir ${_OPENSSL_WIN_SEARCH_PATHS})
            if(EXISTS "${_ssl_dir}/include/openssl/ssl.h")
                set(OPENSSL_INCLUDE_DIRS "${_ssl_dir}/include")
                # 始终使用 /MD 版 import library (非 /MDd)
                find_library(_OPENSSL_SSL_LIB NAMES ssl ssleay32
                    PATHS "${_ssl_dir}/lib/VC/x64/MD" "${_ssl_dir}/lib" NO_DEFAULT_PATH)
                find_library(_OPENSSL_CRYPTO_LIB NAMES crypto libeay32
                    PATHS "${_ssl_dir}/lib/VC/x64/MD" "${_ssl_dir}/lib" NO_DEFAULT_PATH)
                if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB)
                    set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
                endif()
                # DLL
                find_file(_OPENSSL_SSL_DLL NAMES libssl-3-x64.dll libssl-1_1-x64.dll
                    PATHS "${_ssl_dir}/bin" NO_DEFAULT_PATH)
                find_file(_OPENSSL_CRYPTO_DLL NAMES libcrypto-3-x64.dll libcrypto-1_1-x64.dll
                    PATHS "${_ssl_dir}/bin" NO_DEFAULT_PATH)
                if(_OPENSSL_SSL_DLL AND _OPENSSL_CRYPTO_DLL)
                    list(APPEND OPENSSL_DLLS ${_OPENSSL_SSL_DLL} ${_OPENSSL_CRYPTO_DLL})
                endif()
                break()
            endif()
        endforeach()
    else()
        # 非 Windows: Apple arm64 先显式找 Homebrew 原生前缀 (/opt/homebrew)。
        # 机器上常残留 Intel 版 /usr/local/Cellar OpenSSL, CMake 内置 FindOpenSSL
        # 会选中它 —— arm64 链接时 ld 无法使用 x86_64 dylib, 症状是报一批
        # "_SSL_xxx undefined symbols for architecture arm64" (Debug 全量首次链接才暴露)。
        if(APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            foreach(_hb_prefix "/opt/homebrew/opt/openssl@3" "/opt/homebrew/opt/openssl")
                find_library(_OPENSSL_SSL_LIB NAMES ssl PATHS "${_hb_prefix}/lib" NO_DEFAULT_PATH)
                find_library(_OPENSSL_CRYPTO_LIB NAMES crypto PATHS "${_hb_prefix}/lib" NO_DEFAULT_PATH)
                if(_OPENSSL_SSL_LIB AND _OPENSSL_CRYPTO_LIB
                        AND EXISTS "${_hb_prefix}/include/openssl/ssl.h")
                    set(OPENSSL_INCLUDE_DIRS "${_hb_prefix}/include")
                    set(OPENSSL_LIBRARIES ${_OPENSSL_SSL_LIB} ${_OPENSSL_CRYPTO_LIB})
                    message(STATUS "OpenSSL: using Homebrew ARM64 ${_hb_prefix}")
                    break()
                endif()
            endforeach()
            unset(_OPENSSL_SSL_LIB CACHE)
            unset(_OPENSSL_CRYPTO_LIB CACHE)
        endif()
        # 回退: 使用 CMake 内置 FindOpenSSL
        if(NOT OPENSSL_LIBRARIES)
            include(${CMAKE_ROOT}/Modules/FindOpenSSL.cmake RESULT_VARIABLE _BUILTIN_OPENSSL_FOUND)
            if(_BUILTIN_OPENSSL_FOUND AND OpenSSL_FOUND)
                if(NOT OPENSSL_INCLUDE_DIRS AND OPENSSL_INCLUDE_DIR)
                    set(OPENSSL_INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR}")
                endif()
            endif()
        endif()
    endif()
endif()

# ========== 版本检测 ==========

# 从 openssl/opensslv.h 读取版本号
# OpenSSL 3.x: OPENSSL_VERSION_MAJOR/MINOR/PATCH
# OpenSSL 1.1: OPENSSL_VERSION_NUMBER (如 0x10101000L)
if(OPENSSL_LIBRARIES AND OPENSSL_INCLUDE_DIRS)
    set(_OPENSSL_VERSION_H "${OPENSSL_INCLUDE_DIRS}/openssl/opensslv.h")
    if(EXISTS "${_OPENSSL_VERSION_H}")
        # 注意: opensslv.h 中这些宏在条件编译块内，写作 "# define"（# 与 define 间有空格），
        # 因此正则用 "#[ \t]*define" 兼容 "#define" 和 "# define" 两种写法。
        file(STRINGS "${_OPENSSL_VERSION_H}" _OPENSSL_VERSION_STRINGS
            REGEX "#[ \t]*define[ \t]+OPENSSL_VERSION_(MAJOR|MINOR|PATCH|NUMBER)")
        # OpenSSL 3.x 优先
        set(_OPENSSL_VERSION_MAJOR "")
        set(_OPENSSL_VERSION_MINOR "")
        set(_OPENSSL_VERSION_PATCH "")
        foreach(_line ${_OPENSSL_VERSION_STRINGS})
            if(_line MATCHES "#[ \t]*define[ \t]+OPENSSL_VERSION_MAJOR[ \t]+([0-9]+)")
                set(_OPENSSL_VERSION_MAJOR "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "#[ \t]*define[ \t]+OPENSSL_VERSION_MINOR[ \t]+([0-9]+)")
                set(_OPENSSL_VERSION_MINOR "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "#[ \t]*define[ \t]+OPENSSL_VERSION_PATCH[ \t]+([0-9]+)")
                set(_OPENSSL_VERSION_PATCH "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        if(_OPENSSL_VERSION_MAJOR)
            set(OPENSSL_VERSION "${_OPENSSL_VERSION_MAJOR}.${_OPENSSL_VERSION_MINOR}.${_OPENSSL_VERSION_PATCH}")
        else()
            # 回退: OpenSSL 1.1 使用 OPENSSL_VERSION_NUMBER (十六进制)
            file(STRINGS "${_OPENSSL_VERSION_H}" _OPENSSL_VERSION_NUMBER_LINE
                REGEX "#[ \t]*define[ \t]+OPENSSL_VERSION_NUMBER")
            if(_OPENSSL_VERSION_NUMBER_LINE MATCHES "0x([0-9a-fA-F]+)")
                # 解析 0xMNN00PP0L 格式: M=major, NN=minor, PP=patch
                set(_hex "${CMAKE_MATCH_1}")
                math(EXPR _major "${_hex} >> 28")
                math(EXPR _minor "(${_hex} >> 20) & 0xFF")
                math(EXPR _patch "(${_hex} >> 4) & 0xFF")
                set(OPENSSL_VERSION "${_major}.${_minor}.${_patch}")
            endif()
        endif()
    endif()
endif()

# ========== 验证 ==========

find_package_handle_standard_args(OpenSSL
    REQUIRED_VARS OPENSSL_LIBRARIES OPENSSL_INCLUDE_DIRS
    VERSION_VAR OPENSSL_VERSION
)

# 输出信息
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found:")
    message(STATUS "  Include: ${OPENSSL_INCLUDE_DIRS}")
    message(STATUS "  Library: ${OPENSSL_LIBRARIES}")
    message(STATUS "  Static:  ${OPENSSL_IS_STATIC}")
    if(OPENSSL_DLLS)
        message(STATUS "  DLLs:    ${OPENSSL_DLLS}")
    endif()
    if(OPENSSL_VERSION)
        message(STATUS "  Version: ${OPENSSL_VERSION}")
    endif()
else()
    message(STATUS "OpenSSL not found")
    if(ANDROID)
        message(STATUS "  For Android, install OpenSSL to AVOX_EXTERNAL_LIBRARY_DIR/3rdparty/library/android/openssl/")
    elseif(WIN32)
        message(STATUS "  For Windows, install OpenSSL or set OPENSSL_ROOT_DIR")
    endif()
endif()

# 清理内部变量
unset(_OPENSSL_EXTERNAL_PATHS)
unset(_OPENSSL_FOUND_DIR)
unset(_OPENSSL_ARCH_DIR)
unset(_OPENSSL_SSL_LIB CACHE)
unset(_OPENSSL_CRYPTO_LIB CACHE)
unset(_OPENSSL_SSL_DLL CACHE)
unset(_OPENSSL_CRYPTO_DLL CACHE)
unset(_OPENSSL_USE_BUILTIN)
unset(_saved_module_path)

mark_as_advanced(OPENSSL_INCLUDE_DIRS OPENSSL_LIBRARIES OPENSSL_DLLS OPENSSL_IS_STATIC)
