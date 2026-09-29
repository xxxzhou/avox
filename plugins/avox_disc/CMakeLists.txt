# avox_disc 插件: 蓝光原盘(ISO/BDMV) 标题列表 + 直读播放
# loadModule 时把 IoPlan::disc 数据源工厂注册到 AvoxManager.ioSources,
# cmdOpen 对 ".iso" 自动分道(MediaPlayer.cpp), resolve 写 disc.title 选标题。
# 播放腿 IOParseDisc: libbluray bd_read 直读 + FFmpeg 自定义 avio(mpegts)解封装。
#
# libbluray 引用方式与 libtorrent 一致: 独立项目预编译进库仓
#   源项目: D:/Work/github/libbluray (MSYS2 autotools --disable-bd-j;
#           四步配方见本目录 REBUILD.md)
#   产物:   ../avox_library/3rdparty/library/windows/libbluray/{include,lib,bin}
# 由顶层 AVOXOptions.cmake find_package(Libbluray) 解析(AVOX_ENABLE_DISC 关闭时降级)
if(NOT AVOX_ENABLE_DISC)
  message(STATUS "avox_disc plugin: 未启用, 跳过")
  return()
endif()

set(_DISC_LIBS ${LIBBLURAY_LIBRARIES})
register_plugin(avox_disc DYNAMIC LIBS ${_DISC_LIBS} DEP_DLLS "${LIBBLURAY_BIN_DLL}")
# bluray 头只在插件内使用, 不走全局 include_directories(与 libtorrent 的全局式不同)
target_include_directories(avox_disc PRIVATE ${LIBBLURAY_INCLUDE_DIRS})
