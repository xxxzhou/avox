#pragma once

#include <GLES2/gl2.h>

#include <atomic>
#include <memory>
#include <mutex>

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
  // DV 整形(1005): Y2Y 变体程序 —— `samplerExternal2DY2YEXT` 采样拿**未经驱动
  // 色彩转换的原始 YUV**(GL_EXT_YUV_target), 在 shader 内跑与 VK V5/DX11 CS
  // 同源的 DV 链(reshape→ycc_to_rgb→PQ→LMS→回编码 PQ)。OES 常规采样拿到的已是
  // 驱动 YUV→RGB 结果(YCbCr 域丢失), 故 DV 必须换采样器类型 = 换程序
  DoviMeta doviMeta;
  std::atomic<bool> bDovValid{false};
  std::atomic<bool> bDvUboDirty{false};  // 场景级: 只在元数据变时重传 UBO
  std::mutex dvMtx;      // doviMeta 跨线程(播放线程写/渲染线程取, 场景级)
  uint32_t glDvProgram = 0;
  uint32_t dvUboBuf = 0;
  int32_t dvPosAttr = 0;
  int32_t dvUvAttr = 0;
  int32_t dvTexAttr = 0;
  uint32_t dvUboBlock = 0;
  bool bDvUnsupported = false;  // 编译/链接失败(无 EXT_YUV_target)后不再重试

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
  // DV 整形消费点(1005): 原 OES 域限制已由 GL_EXT_YUV_target 的 Y2Y 采样解除
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
  // DV 变体程序(Y2Y 采样): 惰性首见 DV 帧才建; 无 EXT_YUV_target 则置
  // bDvUnsupported 回落常规程序(画面同旧行为, 不报错刷屏)
  bool ensureDvProgram();
  // DV UBO 上传(场景级脏标记驱动, 与 VK/DX11 共用 packDoviUbo)
  void uploadDvUbo();

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