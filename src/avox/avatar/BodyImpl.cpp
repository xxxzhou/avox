#include "BodyImpl.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void addBodyOb(IBody* body, IBodyOb* ob) {
  if (body) {
    auto* impl = dynamic_cast<BodyImpl*>(body);
    if (impl) {
      impl->addObserver(ob);
    }
  }
}

void removeBodyOb(IBody* body, IBodyOb* ob) {
  if (body) {
    auto* impl = dynamic_cast<BodyImpl*>(body);
    if (impl) {
      impl->removeObserver(ob);
    }
  }
}

// 通过 AvoxManager 工厂表创建视频→身体 推理器 (组件 loadModule 时注册 "mediapipe_body"),
// 组件未注册返回 nullptr
IBody* createBody(const char* type) {
  if (!type) {
    return nullptr;
  }
  ModuleMgr::Get().ensureStarted();
  return AvoxManager::Get().bodyHub.create(type);
}

}
