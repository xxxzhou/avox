#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

// 统一 Observer: IRecorderOb 同时用于 IMediaMuxer 和 IRecorder
class JsRecorderOb : public IRecorderOb, public JsObserver {
 public:
  JsRecorderOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsRecorderOb() {}
  void onStateChange(RecorderState preState, RecorderState state) override {
    queueCallback("onStateChange", preState, state);
  }
  void onProgress(const RecorderProgress& progress) override {
    queueCallback("onProgress", progress.currentTimeMs, progress.totalTimeMs);
  }
  void onIoError(AVError error, const char* msg) override {
    queueCallback("onIoError", error, msg);
  }
  void onEncodeError(TrackType trackType, EncodeResult error) override {
    queueCallback("onEncodeError", trackType, error);
  }
  void onComplete() override { queueCallback("onComplete"); }
};

IRecorderOb* createJsRecorderOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsRecorderOb(env, observer);
}

}
