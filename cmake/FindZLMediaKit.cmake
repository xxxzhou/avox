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
    if(NOT ZLTOOLKIT_LIBS)
        message(WARNING "libZLToolKit.a not found for iOS link")
    endif()
    list(APPEND ZLMEDIAKIT_LIBRARIES ${ZLTOOLKIT_LIBS})
    # force_load 旗标单独存放: ZLMEDIAKIT_LIBRARIES 会被 avox_run_module_copy 逐项 file(COPY), 不能混入非路径项
    file(GLOB ZLM_ALL_ARCHS "${Mediakit_LIB_DIR}/*.a")
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

# ZLMediaKit 的 ZLToolKit 依赖 OpenSSL
# OpenSSL 已由 AVOXOptions.cmake 统一查找（在 ZLMediaKit 之前），
# 确保所有模块（ZLMediaKit、Agent/cpp-httplib）使用同一个 OpenSSL 版本。
# 若 AVOXOptions 未启用查找（理论上不会发生），则在此回退查找。
if(ZLMEDIAKIT_FOUND)
    if(NOT AVOX_OPENSSL_FOUND)
        find_package(OpenSSL QUIET)
    endif()
    if(OpenSSL_FOUND)
        # mk_api.dll 自带 OpenSSL 运行时依赖（其内部已链 OpenSSL）。
        # 仅当 avox.dll 本身需要 OpenSSL（Agent 未用 BoringSSL）时，才把 OpenSSL
        # 链进 avox.dll 并加全局 include。Agent 用 BoringSSL 时 avox.dll 不应链 OpenSSL，
        # 否则会与 webrtc 静态链入的 BoringSSL 发生 SSL_* 符号撞车。
        if(NOT AVOX_AGENT_USE_BORINGSSL)
            message(STATUS "OpenSSL linked for ZLMediaKit: ${OPENSSL_LIBRARIES}")
            list(APPEND ZLMEDIAKIT_LIBRARIES ${OPENSSL_LIBRARIES})
            list(APPEND ZLMEDIAKIT_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIRS})
        else()
            message(STATUS "Agent 用 BoringSSL，OpenSSL 仅 mk_api.dll 运行时需要，不链入 avox.dll")
        endif()
    else()
        message(WARNING "OpenSSL not found, ZLMediaKit SSL features disabled")
    endif()
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
