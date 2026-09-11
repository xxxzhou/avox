# avox_remote 插件: 远程内容源统一会话 (IRemoteSource, AvoxBase.h)
# 当前实现: "dav" WebDAV 目录树(兼容 Alist/OpenList 的 /dav 挂载端点)
#   PROPFIND 列目录 + Basic 认证 + resolve 产 http(s) 直链(走现有 http IO 播放)
# 规划中: "smb" (需 libsmb2 预编译入库, 依赖方式与 libtorrent 一致, 见 README)
#
# httplib 为 header-only, 仅需头路径; https 需要 OpenSSL(全局 find_package, 见
# AVOXOptions.cmake; 头文件目录在 Agent 分支里才全局加, 插件这里自带以防 Agent 关闭)
if(NOT OPENSSL_FOUND)
  message(STATUS "avox_remote plugin: openssl 未找到, 跳过")
  return()
endif()
include_directories(${AVOX_TRDPARTY}/cpp-httplib)

set(_REMOTE_LIBS ${OPENSSL_LIBRARIES})
if(WIN32)
  # httplib 基础 socket 依赖(与 avox_torrent 同注)
  list(APPEND _REMOTE_LIBS ws2_32 crypt32)
endif()

register_plugin(avox_remote DYNAMIC LIBS ${_REMOTE_LIBS})
