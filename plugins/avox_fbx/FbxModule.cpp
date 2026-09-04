#include "FbxModule.hpp"

#include "FbxSceneImport.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool FbxModule::loadModule(IOption* option) {
  (void)option;
  // ufbx 纯解析无外部依赖, 编进 dll 即可用; 工厂注册进 sceneImportHub
  AvoxManager::Get().sceneImportHub.reg(
      "fbx", []() -> ISceneImport* { return new FbxSceneImport(); });
  return true;
}

AVOX_REGISTER_MODULE(FbxModule, avox_fbx)

}  // namespace avox
