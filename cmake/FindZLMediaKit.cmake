# 查找ZLMediaKit的头文件目录，不可以在当前项目头文件中引用
find_path(ZLMEDIAKIT_INCLUDE_DIR
    NAMES Rtsp/Rtsp.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/src)
find_path(ZLTOOLKIT_INCLUDE_DIR
    NAMES Network/Socket.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/3rdpart/ZLToolKit/src)
find_path(MEDIASEVER_FLV_INCLUDE_DIR
    NAMES mpeg4-avc.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/3rdpart/media-server/libflv/include)
# 添加C导出头文件目录，可以在当前项目头文件中引用 
find_path(ZLMEDIAKIT_INCLUDE_C_DIR
    NAMES mk_mediakit.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/api/include)

set(ZLMEDIAKIT_INCLUDE_DIRS ${ZLMEDIAKIT_INCLUDE_DIR} ${ZLTOOLKIT_INCLUDE_DIR} ${MEDIASEVER_FLV_INCLUDE_DIR} ${ZLMEDIAKIT_INCLUDE_C_DIR})

# android在编译参数里传入
# 设置ZLMediaKit库文件所在目录(release/Debug有二个固定目录，需要注意)
if(WIN32 OR APPLE)
    set(Mediakit_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/release/${CMAKE_SYSTEM_NAME}/${CMAKE_BUILD_TYPE}/${CMAKE_BUILD_TYPE})
    # set(Mediakit_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/release/${CMAKE_SYSTEM_NAME}/release/release)
    find_library_list(ZLMEDIAKIT_LIBRARIES Mediakit_LIB_DIR "mk_api")
elseif(ONLY_LINUX)
    set(Mediakit_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/release/${CMAKE_SYSTEM_NAME_LOWER}/${CMAKE_BUILD_TYPE})
    find_library_list(ZLMEDIAKIT_LIBRARIES Mediakit_LIB_DIR "mk_api")
else()
    set(Mediakit_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/ZLMediaKit/release/${CMAKE_SYSTEM_NAME_LOWER}/${CMAKE_BUILD_TYPE})
    find_library_list(ZLMEDIAKIT_LIBRARIES Mediakit_LIB_DIR "mk_api")
endif()

# iOS: 静态 libmk_api.a 需显式补齐内部依赖库(mac 为 dylib, 依赖在 dylib 内自带解析)
# 经典 ld 对静态库单遍扫描不回溯(Factory.o 引用同库/他库后成员的 mediakit::*_plugin 不解析),
# 对 release 目录下全部 ZLMediaKit 归档 force_load——该目录的库本就是自包含集合
if(APPLE AND IOS)
    find_library_list(ZLMEDIAKIT_LIBRARIES Mediakit_LIB_DIR mk_api zlmediakit flv mov mpeg jsoncpp)
    # 只取 iOS 自己的模块目录, 通配 build/* 会把同机 macOS 构建产物链进 iOS (ld: built for macOS)
    file(GLOB ZLTOOLKIT_LIBS ${PROJECT_SOURCE_DIR}/build/ios/zlmediakit/3rdpart/ZLToolKit/lib/*/libZLToolKit.a)
    # 模拟器变体构建树 (AVOX_BUILD_TAG=sim)
    file(GLOB ZLTOOLKIT_LIBS_SIM ${PROJECT_SOURCE_DIR}/build/ios-sim/zlmediakit/3rdpart/ZLToolKit/lib/*/libZLToolKit.a)
    list(APPEND ZLTOOLKIT_LIBS ${ZLTOOLKIT_LIBS_SIM})
    if(NOT ZLTOOLKIT_LIBS)
        message(WARNING "libZLToolKit.a not found for iOS link")
    endif()
    list(APPEND ZLMEDIAKIT_LIBRARIES ${ZLTOOLKIT_LIBS})
    # force_load 旗标单独存放: ZLMEDIAKIT_LIBRARIES 会被 avox_run_module_copy 逐项 file(COPY), 不能混入非路径项
    # xcode 多配置构建 release 下会再多一层 <CONFIG>/ 目录 (darwin/ios 均如此), 递归收集
    file(GLOB_RECURSE ZLM_ALL_ARCHS "${Mediakit_LIB_DIR}/*.a")
    foreach(ARCH_PATH ${ZLM_ALL_ARCHS})
        list(APPEND ZLMEDIAKIT_LINK_FLAGS "-Wl,-force_load,${ARCH_PATH}")
    endforeach()
endif()

message(STATUS "Mediakit_LIB_DIR: ${Mediakit_LIB_DIR}")
message(STATUS "Mediakit_LIB_PATHS: ${ZLMEDIAKIT_LIBRARIES}")
# 查找ZLMediaKit库和mk_api库
# find_library(ZLMEDIAKIT_LIBRARY
#     NAMES zlmediakit
#     PATHS ${Mediakit_LIB_DIR})

# ZLMEDIAKIT_FOUND变量
include(FindPackageHandleStandardArgs)
# 需要注意ZLMediaKit和文件FindZLMediaKit.cmake要一致，大小写一致
find_package_handle_standard_args(ZLMediaKit DEFAULT_MSG ZLMEDIAKIT_LIBRARIES ZLMEDIAKIT_INCLUDE_DIRS)

# mk_api 的 SSL 归 mk_api 自己: ENABLE_OPENSSL 构建时已把 libssl/libcrypto 刻进 mk_api
# 自己的依赖表, 运行时由部署解决(WIN32 的 OPENSSL_DLLS 拷贝 / android 随包 / linux 系统包)。
# 此处禁止把 OpenSSL 追加进主二进制链接行: 主二进制静态链着 webrtc 的 BoringSSL, 混链会让
# webrtc 的 SSL_*/EVP_* 调用被绑到 libssl/libcrypto, 两套 ABI 互不兼容, 运行期野指针崩溃
# (2026-09-25 mac rtc 打开即崩即此因)。httplib 的 OpenSSL 分支由 AVOXOptions.cmake 按宿主二进制决定。
if(ZLMEDIAKIT_FOUND AND APPLE)
    # APPLE 静态链入(尤其 iOS force_load 全并入 libavox.a)时, 静态 mk_api 的依赖=主 SDK 的依赖:
    # 归档出现 SSL_*/EVP_* 未定义引用即视为带 SSL 构建, configure 期直接拦截
    foreach(_mklib ${ZLMEDIAKIT_LIBRARIES})
        if(_mklib MATCHES "\\.a$" AND EXISTS "${_mklib}")
            execute_process(COMMAND nm -u "${_mklib}"
                COMMAND grep -cE "^_(SSL_|EVP_|OPENSSL_)"
                OUTPUT_VARIABLE _mk_ssl_refs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_mk_ssl_refs AND NOT _mk_ssl_refs EQUAL 0)
                message(FATAL_ERROR "${_mklib} 含 ${_mk_ssl_refs} 个 OpenSSL 未定义引用。"
                    "静态链入的 mk_api 不得带 SSL: 用 ENABLE_OPENSSL=OFF 重编 ZLM, 或改走 ZLToolKit×BoringSSL 路线")
            endif()
        endif()
    endforeach()
endif()

# mk_api.dll是导出C接口的动态库，需要C接口吗？
if(ZLMEDIAKIT_FOUND)
    # 在Windows平台下，如果找到ZLMediaKit库
    if(WIN32)
        # 查找mk_api.dll文件
        find_file(ZK_API_DLLS NAMES mk_api.dll PATHS ${Mediakit_LIB_DIR})
        avox_run_module_copy("${ZK_API_DLLS}")
        # OpenSSL DLL 复制由 FindOpenSSL.cmake 的 OPENSSL_DLLS + AVOXOptions.cmake 负责
    else()
        avox_run_module_copy("${ZLMEDIAKIT_LIBRARIES}")
    endif()
endif()

# iOS 链接旗标(force_load ZLMediaKit 全部归档, 含 ext-codec 的编解码插件)在资源复制之后并入
if(APPLE AND IOS AND ZLMEDIAKIT_LINK_FLAGS)
    list(APPEND ZLMEDIAKIT_LIBRARIES ${ZLMEDIAKIT_LINK_FLAGS})
endif()
