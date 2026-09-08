#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

// webrtc事件(连接状态/首帧/DataChannel/本地SDP/ICE)转发到js层
class JsRtcEventOb : public IRtcEventOb, public JsObserver {
 public:
  JsRtcEventOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsRtcEventOb() {}

  void onConnectionState(RtcConnState state) override {
    queueCallback("onConnectionState", state);
  }
  void onFirstVideoFrame() override { queueCallback("onFirstVideoFrame"); }
  void onDataChannelMsg(const char* data, int32_t size) override {
    queueCallback("onDataChannelMsg", data, size);
  }
  void onLocalSdp(const char* localSdp) override {
    queueCallback("onLocalSdp", localSdp);
  }
  void onIceCandidate(const char* candidate, const char* mid,
                      int mlineIndex) override {
    queueCallback("onIceCandidate", candidate, mid, mlineIndex);
  }
};

IRtcEventOb* createJsRtcEventOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsRtcEventOb(env, observer);
}

}
