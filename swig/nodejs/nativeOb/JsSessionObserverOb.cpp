#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

// ISessionObserver 的 JS 回调包装。
//
// ISessionObserver 的方法都是单向 fire-and-forget (model 在 driver 线程里把事件吐出去,
// JS 侧只负责显示)。所以 queueCallback 的 NonBlockingCall 完全够用, 不引入同步等待语义。
class JsSessionObserverOb : public ISessionObserver, public JsObserver {
 public:
  JsSessionObserverOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsSessionObserverOb() {}

  void onSessionEvent(size_t seq, const char* typeName,
                      const char* eventJson) override {
    // seq 在 Windows 上是 unsigned long (LLP64), 在 POSIX 上是 unsigned long (LP64);
    // 一律转 int64_t 越过 JS Number 的 double 安全精度。typeName 与 eventJson 直接透传。
    queueCallback("onSessionEvent", static_cast<int64_t>(seq), typeName,
                  eventJson);
  }

  void onToken(const char* text) override {
    queueCallback("onToken", text);
  }

  void onReasoning(const char* text) override {
    queueCallback("onReasoning", text);
  }

  void onToolCall(const char* toolName, const char* argsJson) override {
    queueCallback("onToolCall", toolName, argsJson);
  }

  void onToolResult(const char* toolName, const char* resultText,
                    bool ok) override {
    queueCallback("onToolResult", toolName, resultText, ok);
  }

  void onTurnEnd(const char* content, const char* error) override {
    queueCallback("onTurnEnd", content, error);
  }

  void onStatus(int status) override {
    queueCallback("onStatus", status);
  }
};

ISessionObserver* createJsSessionObserverOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsSessionObserverOb(env, observer);
}

}