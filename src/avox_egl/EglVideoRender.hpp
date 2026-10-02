#pragma once

#include <GLES2/gl2.h>

#include <atomic>
#include <memory>

#include "GLESContext.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/video/ColorSpace.hpp"
#include "avox/video/VideoRender.hpp"
#include "avox/subtitle/CanvasRender.hpp"

#ifdef __ANDROID__
#include "avox_android/AndVDecoder.hpp"
#include "avox_android/SharedGpuBuffer.hpp"
#endif
namespace avox {

class GlesCanvasLayer;

class EglVideoRender : public VideoRender, public GLESContext {
public:
  EglVideoRender();
  virtual ~EglVideoRender();

private:
#ifdef __ANDROID__
  std::unique_ptr<SharedGpuBuffer> sharedBuffer;
  GLESContext *frameRCtx = nullptr;
#endif
  // 传入的EGL上下文
  EGLContext frameCtx = EGL_NO_CONTEXT;
  float aspect = 0.0f;

protected:
  uint32_t glProgram = 0;
  uint32_t fboId = 0;
  int32_t posAttr = 0;
  int32_t uvAttr = 0;
  int32_t extAttr = 0;
  // 颜色/HDR参数(与Dx11CSVideoRender/MetalRender同策略), 每帧uniform下发
  ColorSpaceDesc cs;
  HdrMeta hdrMeta;
  HdrMode hdrMode = HdrMode::follow;
  int32_t hdrModeAttr = 0;
  int32_t transferAttr = 0;
  int32_t peakNitsAttr = 0;
  int32_t sdrWhiteAttr = 0;

protected:
  virtual void onSetSurface() override;
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual bool fetchFrame(ImageBuffer *imageBuffer) override;
  virtual void setColorSpace(const ColorSpaceDesc& c) override;
  virtual void setHdrMeta(const HdrMeta& meta) override;
  virtual void setHdrMode(HdrMode mode) override;
  // EGL 腿暂不消费 DV 整形(OES 域限制), 只留探针观测元数据到达链路
  virtual void setDoviMeta(const DoviMeta& meta) override;

public:
  virtual IRenderContext *getGpuContext() override;

  // 字幕画布挂口(字幕画布多后端渲染计划 §5.3): lane=0 本腿输出是 VK 对接面
  // 禁挂; GLES 无 HDR 呈现面, 恒 SDR gamma 域, 第二 draw 混合
  virtual ICanvasLayer* enableRenderCanvas() override;
  virtual void disableRenderCanvas() override;

private:
  void createProgram();
  void useProgram(uint32_t oesId);
  void closeProgram();
  // 字幕画布挂/摘同步(渲染线程消费 bCanvasWanted, 内容信号走 CanvasRender)
  void syncCanvasLayer();
  // canvas 第二 draw 程序(惰性首挂才建, 零字幕会话零 GL 对象)
  bool ensureCanvasProgram();

 private:
  // 字幕画布: 宿主持前端(wanted 标志), 渲染线程持层实例
  std::unique_ptr<CanvasRender> canvasRender;
  std::unique_ptr<GlesCanvasLayer> canvasLayer;
  std::atomic<bool> bCanvasWanted{false};
  uint32_t glCanvasProgram = 0;
  int32_t canvasPosAttr = 0;
  int32_t canvasUvAttr = 0;
  int32_t canvasTexAttr = 0;
  int32_t canvasRectAttr = 0;
  int32_t canvasXformAttr = 0;
};

}