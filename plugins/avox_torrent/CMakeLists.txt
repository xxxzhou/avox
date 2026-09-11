# avox_torrent 插件: 磁力/BT 边下边播
# loadModule 时把 IoPlan::torrent 数据源工厂注册到 AvoxManager.ioSources,
# 业务 setIoPlan(IoPlan::torrent) + open("magnet:?xt=urn:btih:..."/"xxx.torrent") 即可播放。
# 内部 libtorrent 顺序下载 + FFmpeg 自定义 avio 直读 piece 解封装(无本地HTTP中转)。
#
# libtorrent 引用方式与 opencv/onnxruntime 一致: 独立项目预编译进库仓
#   源项目: D:/Work/github/libtorrent (python build_windows.py)
#   产物:   ../avc_library/3rdparty/library/windows/libtorrent/{include,lib}
# 由顶层 AVOXOptions.cmake find_package(Libtorrent) 解析(AVOX_ENABLE_TORRENT 关闭时降级)
if(NOT AVOX_ENABLE_TORRENT)
  message(STATUS "avox_torrent plugin: 未启用, 跳过")
  return()
endif()

# 需要源码版 FFmpeg 头与库(avio_alloc_context 自定义IO + avformat 解封装),
# 变量由顶层 AVOXOptions.cmake 的 find_package(FFmpeg) 提供, 目录作用域向下可见
if(NOT FFMPEG_LIBRARIES)
  message(STATUS "avox_torrent plugin: ffmpeg 库未找到, 跳过")
  return()
endif()

# 元数据 HTTP 缓存通道(httplib https, itorrents.org)需要 OpenSSL;
# Windows 下 httplib 基础 socket 还要 ws2_32/crypt32
set(_TORRENT_LIBS ${LIBTORRENT_LIBRARIES} ${FFMPEG_LIBRARIES})
# android: webrtc(BoringSSL) 已静态编入 avox, 且经 LINK_ONLY 传播到本链接行,
# 再链 OpenSSL 必撞 SSL_* 符号(duplicate symbol 实测); libtorrent 的 SSL 引用
# 由 avox 内的 BoringSSL 解析。android 无 webrtc 时仍需 OpenSSL 兜底
if(NOT ANDROID OR NOT AVOX_AGENT_USE_BORINGSSL)
  if(OPENSSL_FOUND)
    list(APPEND _TORRENT_LIBS ${OPENSSL_LIBRARIES})
  endif()
endif()
if(WIN32)
  list(APPEND _TORRENT_LIBS ws2_32 crypt32)
endif()

register_plugin(avox_torrent DYNAMIC LIBS ${_TORRENT_LIBS})
