#pragma once

#include <string>

#include "mk_httpclient.h"
#include "avox/AvoxPlayer.h"

namespace avox {

// HTTP SDP信令交换: 将本地SDP POST到信令服务器, 解析远端SDP后调player->setRemoteSdp
// 替代原ZlTestSdpOb(改名, 去掉Zl前缀, 去掉#if AVOX_ENABLE_ZLMEDIAKIT守卫)
// 留在核心层: IRtcPlayer已在AvoxPlayer.h, mk_http_requester(avox_zlmediakit已在用)
class TestSdpOb : public ISdpAgentOb {
 public:
  explicit TestSdpOb(const char* serverUrl);
  virtual ~TestSdpOb();

  void setPlayer(IRtcPlayer* player_) { player = player_; }
  void onLocalSdp(const char* sdp) override;

  // 公有成员供C回调访问
  IRtcPlayer* player = nullptr;
  mk_http_requester requester = nullptr;
  std::string server;

 private:
  friend void onTestHttpComplete(void* user_data, int code, const char* err_msg);
};

}
