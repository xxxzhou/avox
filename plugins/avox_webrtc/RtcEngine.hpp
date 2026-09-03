#pragma once

#include <memory>
#include <mutex>

#include "RtcHelper.hpp"
#include "api/peer_connection_interface.h"


namespace avox {

class RtcEngine {
 public:
  static RtcEngine& Get();
  // 懒加载初始化 WebRTC 运行环境
  bool ensureInitialized();
  // 获取全局唯一的工厂引用
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> getFactory() {
    return factory;
  }
  // 程序退出时清理
  void uninit();

 private:
  RtcEngine() = default;
  ~RtcEngine();

  RtcEngine(const RtcEngine&) = delete;
  RtcEngine& operator=(const RtcEngine&) = delete;

  std::mutex mutex;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory =
      nullptr;

  // M138 推荐使用 std::unique_ptr 管理 webrtc::Thread
  std::unique_ptr<webrtc::Thread> network_thread = nullptr;
  std::unique_ptr<webrtc::Thread> worker_thread = nullptr;
  std::unique_ptr<webrtc::Thread> signaling_thread = nullptr;
};

}