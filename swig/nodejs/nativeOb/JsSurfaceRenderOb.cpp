#include <mutex>

#include "../../../src/avox/video/WindowRender.hpp"
#include "AvoxJsOb.h"
#include "JsObserver.hpp"

using namespace v8;

namespace avox {

class JsSurfaceRenderOb : public JsObserver, public ISurfaceRenderOb {
 public:
  JsSurfaceRenderOb(Napi::Env env, Napi::Value observer)
      : JsObserver(env, observer) {
    Napi::HandleScope scope(env);
    Napi::Object obj = jsObject.Value();
    // 确定是否JS层管理内存
    if (obj.Has("jsManagedBuffer")) {
      Napi::Value val = obj.Get("jsManagedBuffer");
      // 转换为 C++ 的 bool
      jsManagedBuffer = val.As<Napi::Boolean>().Value();
    }
    log(LogLevel::info, "jsManagedBuffer:", jsManagedBuffer);
  }
  virtual ~JsSurfaceRenderOb() {
    if (!jsBufferRef.IsEmpty()) {
      jsBufferRef.Reset();
    }
    rawBufferPtr = nullptr;
  }

 private:
  Napi::Reference<Napi::Uint8Array> jsBufferRef;
  uint8_t* rawBufferPtr = nullptr;
  int32_t rawBufferSize = 0;
  bool jsManagedBuffer = false;
  std::mutex mtx;

 public:
  virtual void onSurface() override { queueCallback("onSurface"); }
  virtual void onWinSizeChange(int32_t width, int32_t height) override {
    queueCallback("onWinSizeChange", width, height);
  };
  // Js帧数据
  void setJsBuffer(Napi::Uint8Array buffer) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!jsBufferRef.IsEmpty()) {
      jsBufferRef.Reset();
    }
    jsBufferRef = Napi::Persistent(buffer);
    rawBufferPtr = buffer.Data();
    rawBufferSize = buffer.ByteLength();
  }
  // 需要windowrender打开enableYuvOut,electron返回CPU数据给网页
  virtual void onFrame(const YUVFrame& frame) override {
    // log(LogLevel::info, "onFrame:", frame.format);
    uint8_t* targetPtr = nullptr;
    int32_t targetSize = 0;
    // onFrame一次,才会转JS线程回调请求一次setJsBuffer
    // 所以在memcpy时,下一个setJsBuffer肯定不会到
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (rawBufferPtr) {
        targetPtr = rawBufferPtr;
        targetSize = rawBufferSize;
      }
    }
    int32_t frameSize = getYuvFrameSize(frame.format, frame.stride[0]);
    if (targetPtr && targetSize >= frameSize) {
      // 从vulkan返回的frame肯定是nv12格式并且紧湊的
      if (bTightlyPacked(frame)) {
        // log(LogLevel::info, "Frame is tightly packed, using memcpy");
        memcpy(targetPtr, frame.data[0], frameSize);
      } else {
        // log(LogLevel::info, "Frame is no tightly packed, using memcpy");
        copyPlaneYUV2TightlyBuffer(frame, targetPtr);
      }
    }
    // 让JS层知道需要帧大小,申请JS层内存并传入需要写入指针
    queueCallback("onFrame", frame);
  };
};

ISurfaceRenderOb* createJsSurfaceRenderOb(Napi::Value observer) {
  Napi::Env env = observer.Env();
  return new JsSurfaceRenderOb(env, observer);
}

void setRenderJsBuffer(ISurfaceRenderOb* winOb, Napi::Value buffer) {
  JsSurfaceRenderOb* jsWinOb = static_cast<JsSurfaceRenderOb*>(winOb);
  if (!jsWinOb) {
    return;
  }
  Napi::Uint8Array jsBuffer = buffer.As<Napi::Uint8Array>();
  jsWinOb->setJsBuffer(jsBuffer);
}

}