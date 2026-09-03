# Swig 与 Node.js 原生扩展的跨线程回调

当前项目用 Swig 生成 C#,java,js 等语言，前面 C#/java 使用 swig 的 director 生成相应类的回调都很方便，于是直接在 node js 里也使用类似的回调方式，不出意外失败了，简单了解了下，发现是 nodejs 的跨线程问题，简单说明下流程，当播放器如打开流后，回调流信息，肯定是在播放器自身线程上，如何把回调给到 JS 对象上，因为 JS 对象的调用必需在 Node.js 主线程上，然后在主线程执行 JS 代码。

网上找了下实现的回调方式，大部分是手动全封装，但是当前项目，实现的接口已经很多了，使用 swig 只是没能正常生成回调，别的封装还是和别的语言一样，当前主要实践如何结合 swig 自动生成的类，把 js 回调对象绑定到 C++类的回调对象上，假定下面是对应的播放器回调接口。

```c++
class IMediaPlayerOb {
public:
  IMediaPlayerOb() = default;
  virtual ~IMediaPlayerOb() = default;

public:
  // 播放器状态变化
  virtual void onStateChange(PlayerState preState, PlayerState state) {};
  // IO错误返回
  virtual void onIoError(AVError error) {}
  // 解码错误返回
  virtual void onDecodeError(TrackType trackType, DecodeResult error) {}

  // Track信息准备好了
  virtual void onReady() {}
  // 完成
  virtual void onComplete() {}
  virtual void onSeek() {}
  virtual void onPause() {}
  virtual void onResume() {}
  virtual void onClose() {}
};
extern "C" {
AVOX_EXPORT IMediaPlayer *createMediaPlayer();
AVOX_EXPORT void addMediaPlayerOb(IMediaPlayer *player, IMediaPlayerOb *ob);
AVOX_EXPORT void removeMediaPlayerOb(IMediaPlayer *player, IMediaPlayerOb *ob);
}
```

因为 IMediaPlayer 已经由 Swig 自动生成，以及与回调类的 API 绑定，C#和 Java 中继承声明一个类继承 IMediaPlayerOb 就行了，然后调用绑定 API 就可以了，而 JS 中，想实现类似的流程，首先需要把一个 JS 的对象映射到实际的 IMediaPlayerOb 对象中，并且在 C++的回调在转发 JS 对象上时自动转到主线程上。

因此确定流程如下:

1. js 生成回调类，回调类方法需要同 C++回调类里方法名一致。
2. 生成 nodejs 的 C++回调封装类，其一是接收 js 对象，二是实现 C++如上 IMediaPlayerOb 的回调方法。其主要是在 C++的回调被调用后，把方法与参数统一保存，并通过 N-API 的 ThreadSafeFunction 将回调调度到 Node.js 主线程，映射到 js 对象与参数上，调用 js 对象方法与参数。
3. 在 swig 自动生成的方法中，添加一个方法，传入 js 对象，返回上面中转的 C++的回调类。
4. js 调用上面的方法，然后按照统一的 C#/java 等语言的方式，使用如 addMediaPlayerOb 方法绑定播放类与播放回调。

这种方式同别的语言一样，调用 swig 生成的同样 API，只是需要添加额外的方法来绑定 JS 对象到 C++的回调类上。

根据上面流程，先来看现在实现的前端代码，首先是 js 回调类。

```js
class JsMediaPlayerOb {
  constructor(player) {
    this.player = player;
  }
  onStateChange(preState, state) {
    avox.logMsg(0, `jsob onStateChange state changed: ${preState} -> ${state}`);
  }
  onIoError(error) {}
  onDecodeError(trackType, error) {}
  onReady() {
    const sourceInfo = this.player.getSourceInfo();
    avox.logMsg(0, `jsob media player ready`);
    if (sourceInfo.videoSize() > 0) {
      const vtrackDesc = sourceInfo.getVideoDesc(0);
      const vcodename = avox.getVCodecName(vtrackDesc.codecId);
      const yuvTypeStr = avox.getYuvTypeStr(vtrackDesc.desc.type);
      avox.logMsg(
        0,
        `jsob video codec:${vcodename},video width:${vtrackDesc.desc.width},height:${vtrackDesc.desc.height},fps:${vtrackDesc.desc.fps},yuv:${yuvTypeStr}`
      );
    }
    if (sourceInfo.audioSize() > 0) {
      const atrackDesc = sourceInfo.getAudioDesc(0);
      avox.logMsg(
        0,
        `jsob audio codec:${avox.getACodecName(
          atrackDesc.codecId
        )}, audio channels:${atrackDesc.desc.channels},sampleRate:${
          atrackDesc.desc.sampleRate
        },sampleFormat:${avox.getAudioFormatStr(atrackDesc.desc.format)}`
      );
    }
  }
  onClose() {
    avox.logMsg(0, `jsob media player close`);
  }
}
class PlayerMain {
  // 初始化媒体播放器
  initializeMediaPlayer() {
    // 得到BrowserWindow的原生窗口句柄
    const nativeHandle = this.mediaWindow
      .getNativeWindowHandle()
      .readUInt32LE(0);
    // 创建mediaPlayer实例
    this.mediaPlayer = avox.createMediaPlayer();
    // 底层显示
    avox.setElectornSurface(this.mediaPlayer.getSurfaceRender(), nativeHandle, true);
    // 得到player里的mediaMuxer实例
    this.mediaMuxer = this.mediaPlayer.getMuxer();
    // JS回调对象
    const observer = new JsMediaPlayerOb(this.mediaPlayer);
    // JS回调对象转换成C++的回调对象
    this.callback = avox.createJsMediaPlayerOb(observer);
    // 播放器绑定回调对象
    avox.addMediaPlayerOb(this.mediaPlayer, this.callback);
  }
}
```

输出回调。

```txt
[11:44:04.950] info: jsob media player ready
[11:44:04.951] info: task run:audio decode task thread id 9760
[11:44:04.951] info: jsob video codec:h264,video width:1920,height:1080,fps:23.976,yuv:yuv420P
[11:44:04.951] info: FdkaacDecoder.cpp:56 onPreDecoder fdk-aac init asc objectType:2 sampleRateIndex:8 channel:2
[11:44:04.951] info: jsob audio codec:aac, audio channels:2,sampleRate:16000,sampleFormat:fltp
```

swig 里在导出 js 封装文件里添加方法，用于 js 对象转 C++的回调类。

```c++
// AvoxJsOb.h
#pragma once

#include <napi.h>
#include <node.h>

#include "../../../src/avox/AvoxLog.h"
#include "../../../src/avox/AvoxPlayer.h"
// avox_webrtc 已迁 plugins/avox_webrtc; IRtcPlayer/createWebRtcPlayer/createZlTestSdpAgent
// addRtcPlayerOb/removeRtcPlayerOb 均由 avox/AvoxPlayer.h 导出(AVOX_EXPORT)

namespace avox {

extern "C" {
// JS 侧只需传入一个 observer 对象
IMediaPlayerOb* createJsMediaPlayerOb(Napi::Value observer);
ISdpAgentOb* createJsSdpAgentOb(Napi::Value observer);
ILogOb* createJsLogOb(Napi::Value observer);
ISurfaceRenderOb* createJsWindowRenderOb(Napi::Value observer);
IRecorderOb* createJsRecorderOb(Napi::Value observer);
IMediaMuxerOb* createJsMuxerOb(Napi::Value observer);
}

}
```

由于 Napi::Value 是 node-addon-api 的类型，swig 需要明确的类型映射规则来处理 JavaScript 对象到 C++ N-API 对象的转换。

```c++
// 添加JsMediaPlayerOb相关的头文件包含,只有js需要
#ifdef SWIGJAVASCRIPT
#include "nodejs/nativeOb/AvoxJsOb.h"
#endif
#ifdef SWIGJAVASCRIPT
%{
#include <napi.h>
#include <windows.h>
%}
%typemap(in) Napi::Value {
  $1 = $input; // $input 是 SWIG 包装层传入的 Napi::Value
}
// 添加JsOb方法的导出声明
#ifdef SWIGJAVASCRIPT
%include "nodejs/nativeOb/AvoxJsOb.h"
#endif
#endif
```

在生成 nodejs 原生扩展的 gyp 文件中，添加 jsOb 的相应头文件及实现文件，具体实现见[播放器 Electron](../../doc/media/播放器Electron.md).

```json
{
  "targets": [{
    "target_name": "avox_js",
    "sources": [
      "files/commonJAVASCRIPT_wrap.cxx",
      "nativeOb/JsMediaPlayerOb.cpp",
      "nativeOb/JsSdpAgentOb.cpp",
      "nativeOb/JsLogOb.cpp",
      "nativeOb/JsWindowRenderOb.cpp",
      "nativeOb/JsRecorderOb.cpp",
      "nativeOb/JsMuxerOb.cpp",
      "nativeOb/JsEglHelper.cpp"
    ],
    "libraries": ["-lavox"],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")",
      "../../src",
      "../../swig",
      "../../3rdparty/khronos"
    ],
    "defines": [
      "NODE_ADDON_API_DISABLE_CPP_EXCEPTIONS",
      "NAPI_VERSION=8"
    ]
  }]
}
```

如上就完成了 js 回调对象到 C++回调对象的转换。

最后是 JsMediaPlayerOb 的实现，其基类 JsObserver 用来做流程 2 里的，一是接收 js 对象，并在 C++的回调被调用后，通过 N-API 的 ThreadSafeFunction 将回调调度到 Node.js 主线程执行，映射到 js 对象与参数上，调用 js 对象方法与参数。

```c++
// JsMediaPlayerOb.cpp
#include "AvoxJsOb.h"
#include "JsObserver.hpp"

namespace avox {

class JsMediaPlayerOb : public IMediaPlayerOb, public JsObserver {
public:
  JsMediaPlayerOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {}
  virtual ~JsMediaPlayerOb() {}

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
```

```c++
// JsObserver.hpp
#pragma once

#include <napi.h>  // 使用 node-addon-api

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "../../../src/avox/module/LogHelper.hpp"

namespace avox {

// ================================
//  渲染进程 Node.js 回调基类
//  - 仅使用 N-API (node-addon-api)
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
    tsfn = Napi::ThreadSafeFunction::New(
        env,
        Napi::Function::New(env, [](const Napi::CallbackInfo&){}),
        "JsRenderObTSFN", 0, 1);
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

    // 3. 锁外：执行真正的异步排队（NonBlockingCall 内部有线程安全实现）
    napi_status status = tsfn_local.NonBlockingCall(
        [this, life, methodName, capturedArgs](Napi::Env env, Napi::Function) {
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
    if constexpr (std::is_same_v<T, int> || std::is_enum_v<T>) {
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
    if constexpr (std::is_same_v<std::decay_t<T>, const char*>)
      return std::string(arg ? arg : "");
    else return std::forward<T>(arg);
  }
};

}
```

与之前使用 V8 原生 API (`v8::Isolate`, `v8::Local`, `uv_async_t`) 的实现相比，使用 N-API (`node-addon-api`) 的主要变化：

1. **跨线程调度:** 从 `uv_async_t` + `uv_async_send` 手动管理 libuv 事件循环，改为 `Napi::ThreadSafeFunction::NonBlockingCall`，N-API 内部自动处理线程调度，无需手动管理 uv loop。
2. **对象引用:** 从 `v8::Persistent<v8::Object>` 改为 `Napi::ObjectReference`，从 `v8::Isolate*` + `v8::Local<v8::Object>` 改为 `Napi::Env` + `Napi::Value`。
3. **参数转换:** 从手动 `v8::Integer::New`, `v8::String::NewFromUtf8` 等 V8 API 改为 `Napi::Number::New`, `Napi::String::New` 等 N-API 封装。
4. **生命周期管理:** 使用 `std::shared_ptr<std::atomic<bool>>` 代替原来的 `bool isClosing`，配合 `napi_add_env_cleanup_hook` 防止 Electron Reload 崩溃。
5. **ABI 稳定:** N-API 是 Node.js 提供的 ABI 稳定接口，不受 V8 版本变化影响，编译的原生模块可以在不同 Node.js 版本间复用，无需为每个版本重新编译。

支持常用的参数类型，如 int、enum、std::string、bool、YUVFrame，自定义结构暂时还没用到，后面看需求再配合 swig 生成的结构加到转换里。需要注意的 const char*可能只在当前 C++回调线程有效，如果直接将指针传入，在 JS 回调线程里使用，可能指针已经失效了，所以需要先拷贝一份。

## 从 V8 迁移到 N-API 的原因

最初使用 V8 原生 API (`v8::Isolate`, `v8::Local`, `uv_async_t`) 封装回调，后来全部改为 N-API (`node-addon-api`)，主要基于以下原因：

### 1. V8 封装只能用在主进程，渲染进程必须使用 N-API

原来使用原生窗口渲染，但原生窗口与 Electron 网页上的各种组件无法解决多层叠覆盖等问题，因此改为将视频帧数据传到网页，由 WebGL/WebGPU 渲染。这就要求 C++ 的接口和回调必须在渲染进程（preload.js）中可用，而 V8 原生 API 与主线程的 V8 Isolate 上下文绑定，只能在主进程使用，无法在渲染进程中正常工作。改用 N-API 后，原生模块可以在渲染进程中正常调用 C++ 接口与回调，项目中所有回调类全部改为 N-API，实现了渲染进程在 preload.js 中直接调用 C++ 接口和接收回调的完整流程。

### 2. N-API 下 C++ 重载方法无效

JavaScript 不支持函数重载，同名函数后者会覆盖前者。SWIG 对 JavaScript 生成的重载分发机制很脆弱，主要按参数数量分发，相同数量不同类型的重载容易失败。因此 nodejs 封装的 C++ 类中，不能出现重载方法，相应方法会无效。项目中有以下重载方法在 JS 中无法正常使用：

对于需要暴露给 JS 的重载方法，应使用不同函数名替代，如 `seekToByProgress` / `seekToByPosition`。
