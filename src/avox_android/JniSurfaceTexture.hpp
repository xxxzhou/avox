#pragma once

#include "AndCommon.hpp"
#include "avox/module/RunTask.hpp"

namespace avox {

class JniSurfaceTexture {
public:
  JniSurfaceTexture();
  ~JniSurfaceTexture();

  // 添加帧可用回调接口
  using FrameAvailableCallback = std::function<void()>;

private:
  // 对接上层窗口，用于消费GPU数据，回调
  jobject surfaceTexture = nullptr;
  // 回调
  jobject surfaceTextureOb = nullptr;
  // 对接解码器，用于产生GPU数据
  jobject surface = nullptr;
  ANativeWindow *nativeWindow = nullptr;
  // 帧更新可用回调
  FrameAvailableCallback frameAvailableCallback = nullptr;

public:
  void init(int32_t textureId);
  // 在关闭时,回调不在同一线程,很容易crash
  void close();

  void setImageSize(int32_t width, int32_t height);
  void updateTexImage();
  void attachToGLContext(int32_t texId);
  void detachFromGLContext();
  void getTransformMatrix(float mat[]);

  // 设置帧可用回调
  void setOnFrameAvailableListener(FrameAvailableCallback callback);

public:
  jobject getSurfaceTexture() { return surfaceTexture; }
  jobject getSurface() { return surface; }
  ANativeWindow *getNativeWindow() { return nativeWindow; }
  // java层通过nativeOnFrameAvailable回调到此
  void onFrameAvailable();
};

}