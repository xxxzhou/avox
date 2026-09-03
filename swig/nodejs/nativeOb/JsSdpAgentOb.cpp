#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

// webrtc播放器会收集本地sdp，由js层处理得到远端sdp
// 再调用setRtcRemoteSdp告诉webrtc播放器远端sdp
class JsSdpAgentOb : public ISdpAgentOb, public JsObserver {
 public:
  JsSdpAgentOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsSdpAgentOb() {}

  void onLocalSdp(const char* localSdp) override {
    queueCallback("onLocalSdp", localSdp);
  }
  void onIceCandidate(const char* candidate, const char* mid,
                      int mlineIndex) override {
    queueCallback("onIceCandidate", candidate, mid, mlineIndex);
  }
};

ISdpAgentOb* createJsSdpAgentOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsSdpAgentOb(env, observer);
}

}