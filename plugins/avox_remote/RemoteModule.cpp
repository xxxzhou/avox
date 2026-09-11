#include "RemoteModule.hpp"

#include "DavSource.hpp"
#include "avox/module/AvoxManager.hpp"
// 静态模式(iOS/WASM)下 AVOX_REGISTER_MODULE 展开 StaticLinkModule, 需此头
#include "avox/module/ModuleMgr.hpp"
#ifdef AVOX_REMOTE_SMB
#include "IOParseSmb.hpp"
#include "SmbSource.hpp"
#endif

namespace avox {

bool RemoteModule::loadModule(IOption* option) {
  (void)option;
  // 远程内容源工厂注册: IRemoteSource("dav", 见 AvoxBase.h) WebDAV 目录树+选文件
  // 注: 注册项无显式卸载接口(RegeditObj), 与插件DLL句柄常驻策略配套(进程内不FreeLibrary)
  AvoxManager::Get().remoteSourceHub.reg(
      "dav", []() -> IRemoteSource* { return new DavSource(); });
#ifdef AVOX_REMOTE_SMB
  // IRemoteSource("smb") 目录树+选文件 + IoPlan::smb 播放源工厂
  // (MediaPlayer 对 smb:// 链接自动路由, 业务无需显式 setIoPlan)
  AvoxManager::Get().remoteSourceHub.reg(
      "smb", []() -> IRemoteSource* { return new SmbSource(); });
  IoPlanDesc smbDesc = {};
  smbDesc.name = "smb(libsmb2 streaming)";
  AvoxManager::Get().ioSources.regInitFunc(
      IoPlan::smb, smbDesc, []() -> AVSource* { return new IOParseSmb(); });
#endif
  return true;
}

AVOX_REGISTER_MODULE(RemoteModule, avox_remote)

}
