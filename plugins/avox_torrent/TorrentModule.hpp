#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_torrent 插件入口: loadModule 时把 IoPlan::torrent 数据源工厂注册到
// AvoxManager.ioSources, 业务 setIoPlan(IoPlan::torrent) 后 open 磁力/torrent 即播。
// 与 regZmIO 同构: IOParseTorrent : AVSource, 走标准 IO 包回调管线。
class TorrentModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
