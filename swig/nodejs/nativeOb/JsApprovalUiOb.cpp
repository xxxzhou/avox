#include "AvoxJsOb.h"
#include "JsObserver.hpp"

#include <stdexcept>

using namespace v8;

namespace avox {

// IApprovalUi 的 JS 回调包装。
//
// ask() 在 driver 线程 (非 JS 线程) 同步阻塞: agent 等待审批结果才能决定下一步。
// 我们用 Napi::ThreadSafeFunction::BlockingCall 同步调到 JS 函数, 等 JS 返回 int 答案再
// 回给 C++ 侧。BlockingCall 不会从 JS 线程递归调用, 所以这里安全。
//
//   ask() 返回值约定 (avox/avox_agent/AgentExport.h):
//     0 = AllowedOnce, 1 = Rejected, 2 = Cancelled, 3 = Unavailable (fail-closed)
//
// JS 函数签名: ask(toolName: string, callId: string, reason: string) -> number
class JsApprovalUiOb : public IApprovalUi, public JsObserver {
 public:
  JsApprovalUiOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsApprovalUiOb() {}

  int ask(const char* toolName, const char* callId,
          const char* reason) override {
    int result = 3;  // fail-closed 默认值: Unavailable
    if (lifecycle == nullptr || tsfn == nullptr || jsObject.IsEmpty()) {
      return result;
    }
    auto life = lifecycle;
    const std::string toolNameStr = toolName == nullptr ? "" : toolName;
    const std::string callIdStr = callId == nullptr ? "" : callId;
    const std::string reasonStr = reason == nullptr ? "" : reason;

    Napi::ThreadSafeFunction tsfnLocal;
    {
      std::lock_guard<std::mutex> lock(tsfn_mutex);
      if (!tsfn) return result;
      tsfnLocal = tsfn;
    }

    // BlockingCall: 阻塞 driver 线程直到 JS 函数返回。结果通过引用捕获的 result 传回。
    napi_status status = tsfnLocal.BlockingCall(
        [this, life, toolNameStr, callIdStr, reasonStr, &result](
            Napi::Env env, Napi::Function jsCallback) {
          if (!*life || this->jsObject.IsEmpty()) {
            result = 3;
            return;
          }
          Napi::HandleScope scope(env);
          Napi::Object obj = this->jsObject.Value();
          if (!obj.Has("ask")) {
            result = 3;
            return;
          }
          Napi::Value member = obj.Get("ask");
          if (!member.IsFunction()) {
            result = 3;
            return;
          }
          try {
            Napi::Value value =
                member.As<Napi::Function>().Call(
                    obj, {Napi::String::New(env, toolNameStr),
                          Napi::String::New(env, callIdStr),
                          Napi::String::New(env, reasonStr)});
            // 允许 JS 返回 null / undefined / 非数字 → 按 Unavailable 处理
            if (value.IsNumber()) {
              int32_t n = value.As<Napi::Number>().Int32Value();
              if (n < 0 || n > 3) n = 3;
              result = n;
            } else {
              result = 3;
            }
          } catch (...) {
            result = 3;
          }
        });

    if (status != napi_ok) {
      // 同步调用失败也走 fail-closed
      result = 3;
    }
    return result;
  }
};

IApprovalUi* createJsApprovalUiOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsApprovalUiOb(env, observer);
}

}