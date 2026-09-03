#pragma once

#include <napi.h>  // 使用 node-addon-api 更简洁

#include <mutex>
#include <string>
#include <vector>

#include "../../../src/avox/module/LogHelper.hpp"

namespace avox {

// ================================
//  渲染进程 Node.js 回调基类
//  - 仅使用 N-API
//  - 线程安全
// ================================
class JsObserver {
 public:
  JsObserver(Napi::Env env, Napi::Value jsObjectRef) {
    if (!jsObjectRef.IsObject()) {
      Napi::TypeError::New(env, "Observer must be an object").ThrowAsJavaScriptException();
      return;
    }
    lifecycle = std::make_shared<std::atomic<bool>>(true);
    tsfn = Napi::ThreadSafeFunction::New(env, Napi::Function::New(env, [](const Napi::CallbackInfo&){}), "JsRenderObTSFN", 0, 1);
    jsObject = Napi::Persistent(jsObjectRef.As<Napi::Object>());

    // 环境销毁钩子：防止 Electron Reload 崩溃的关键
    napi_add_env_cleanup_hook(env, [](void* arg) {
      static_cast<JsObserver*>(arg)->Destroy();
    }, this);
  }

  virtual ~JsObserver() {
    napi_remove_env_cleanup_hook(GetEnv(), [](void* arg){}, this);
    Destroy();
  }
  
  void Destroy() {
    std::lock_guard<std::mutex> lock(tsfn_mutex);
    if (lifecycle) *lifecycle = false;
    if (tsfn) {
      tsfn.Abort(); // 立即清空任务队列
      tsfn = nullptr;
    }
    if (!jsObject.IsEmpty()) jsObject.Reset();
  }

  Napi::Env GetEnv() const { return jsObject.Env(); }

 protected:
  Napi::ThreadSafeFunction tsfn;
  Napi::ObjectReference jsObject;
  std::shared_ptr<std::atomic<bool>> lifecycle;
  std::mutex tsfn_mutex;

  template <typename... Args>
  void queueCallback(const std::string& methodName, Args&&... args) {
    // 1. 锁外：准备参数（耗时操作不在锁内）
    auto life = lifecycle;
    auto capturedArgs = std::make_tuple(convertCharPtrToString(std::forward<Args>(args))...);

    Napi::ThreadSafeFunction tsfn_local;
    {      
      // 2. 细粒度加锁：只为了安全地拷贝 tsfn 指针
      std::lock_guard<std::mutex> lock(tsfn_mutex);
      if (!tsfn) return;
      tsfn_local = tsfn; // 增加引用计数，确保调用期间有效
    }

    // 3. 锁外：执行真正的异步排队（NonBlockingCall 内部有自己的线程安全实现）
    napi_status status = tsfn_local.NonBlockingCall([this, life, methodName, capturedArgs](Napi::Env env, Napi::Function) {
      // 4. JS 线程执行：检查对象是否还活着
      if (!*life || this->jsObject.IsEmpty()) return;

      Napi::HandleScope scope(env);
      Napi::Object obj = this->jsObject.Value();
      if (obj.Has(methodName)) {
        Napi::Value member = obj.Get(methodName);
        if (member.IsFunction()) {
          std::vector<napi_value> argv;
          std::apply([this, &env, &argv](auto&&... unpacked) {
            (argv.push_back(this->convertToNapi(env, unpacked)), ...);
          }, capturedArgs);
          member.As<Napi::Function>().Call(obj, argv);
        }
      }
    });
  }

 private:  
  template <typename T>
  napi_value convertToNapi(Napi::Env env, T value) {
    if constexpr (std::is_same_v<T, int64_t>) {
      return Napi::Number::New(env, static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, int> || std::is_enum_v<T>) {
      return Napi::Number::New(env, static_cast<int32_t>(value));
    } else if constexpr (std::is_same_v<T, std::string>) {
      return Napi::String::New(env, value);
    } else if constexpr (std::is_same_v<T, bool>) {
      return Napi::Boolean::New(env, value);
    } else if constexpr (std::is_same_v<std::decay_t<T>, YUVFrame>) {
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("width", value.format.width);
      obj.Set("height", value.format.height);
      int32_t byteWidth = std::max(value.format.width, value.stride[0]);
      obj.Set("stride", byteWidth);
      int32_t frameSize = getYuvFrameSize(value.format, value.stride[0]);
      obj.Set("frameSize", frameSize);
      obj.Set("format", (int32_t)value.format.type);
      return obj;
    }
    return env.Undefined();
  }

  template <typename T>
  auto convertCharPtrToString(T&& arg) {
    if constexpr (std::is_same_v<std::decay_t<T>, const char*>) return std::string(arg ? arg : "");
    else return std::forward<T>(arg);
  }
};

}