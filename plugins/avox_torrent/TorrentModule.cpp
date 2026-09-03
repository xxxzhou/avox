#include "TorrentModule.hpp"

#include "IOParseTorrent.hpp"
#include "TorrentProbe.hpp"
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
  // 数据源探测工厂: ISourceProbe("torrent", 见 AvoxBase.h) 磁力/BT 文件列表+选文件
  AvoxManager::Get().sourceProbeHub.reg(
      "torrent", []() -> ISourceProbe* { return new TorrentProbe(); });
  return true;
}

AVOX_REGISTER_MODULE(TorrentModule, avox_torrent)

}
