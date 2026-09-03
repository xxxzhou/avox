#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

class JsLogOb : public ILogOb, public JsObserver {
 public:
  JsLogOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsLogOb() {}

  void onLogEvent(int level, const char* message) override {
    queueCallback("onLogEvent", level, message);
  }
};

ILogOb* createJsLogOb(Napi::Value observer) {
  Napi::Env env = observer.Env(); 
  return new JsLogOb(env, observer);
}

}