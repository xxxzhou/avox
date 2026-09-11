#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_remote 插件入口: loadModule 时把远程内容源工厂注册到
// AvoxManager.remoteSourceHub ("dav" = DavSource, WebDAV 目录树; 后续 "smb"/"alist")。
// 上层 createRemoteSource("dav") 建会话, open/list/resolve 走 IRemoteSource 统一模型。
class RemoteModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
