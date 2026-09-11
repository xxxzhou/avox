#include "TorrentModule.hpp"

#include "IOParseTorrent.hpp"
#include "TorrentSource.hpp"
#include "avox/module/AvoxManager.hpp"
// 静态模式(iOS/WASM)下 AVOX_REGISTER_MODULE 展开 StaticLinkModule, 需此头
#include "avox/module/ModuleMgr.hpp"

namespace avox {

bool TorrentModule::loadModule(IOption* option) {
  (void)option;
  // 数据源工厂注册: IoPlan::torrent -> new IOParseTorrent()
  // 注: 注册项无显式卸载接口(RegeditObj), 与插件DLL句柄常驻策略配套(进程内不FreeLibrary)
  IoPlanDesc desc = {};
  desc.name = "torrent(magnet/bt streaming)";
  AvoxManager::Get().ioSources.regInitFunc(
      IoPlan::torrent, desc,
      []() -> AVSource* { return new IOParseTorrent(); });
  // 远程内容源工厂: IRemoteSource("torrent", 见 AvoxBase.h) 磁力/BT 目录树+选文件
  AvoxManager::Get().remoteSourceHub.reg(
      "torrent", []() -> IRemoteSource* { return new TorrentSource(); });
  return true;
}

AVOX_REGISTER_MODULE(TorrentModule, avox_torrent)

}
