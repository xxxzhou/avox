#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

class JsMediaPlayerOb : public IMediaPlayerOb, public JsObserver {
public:
  JsMediaPlayerOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsMediaPlayerOb() {}

  // 线程安全的回调方法 - 这些方法在C++线程中调用
  void onStateChange(PlayerState preState, PlayerState state) override {
    queueCallback("onStateChange", preState, state);
  }

  void onIoError(AVError error, const char *msg) override {
    queueCallback("onIoError", error, msg);
  }

  void onDecodeError(TrackType trackType, DecodeResult error) override {
    queueCallback("onDecodeError", trackType, error);
  }
  void onReady() override { queueCallback("onReady"); };

  void onComplete() override { queueCallback("onComplete"); }

  void onSeek() override { queueCallback("onSeek"); }

  void onPause() override { queueCallback("onPause"); }

  void onResume() override { queueCallback("onResume"); }

  void onClose() override { queueCallback("onClose"); }
};

IMediaPlayerOb *createJsMediaPlayerOb(Napi::Value observer) {
  Napi::Env env = observer.Env();  
  return new JsMediaPlayerOb(env, observer);
}

}