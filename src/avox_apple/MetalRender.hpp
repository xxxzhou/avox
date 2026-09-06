#include "avox/video/VideoRender.hpp"
#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include "MetalContext.hpp"

namespace avox {

class MetalRender : public VideoRender, public MetalContext {
public:
  MetalRender();
  virtual ~MetalRender();

 protected:
  id<MTLRenderPipelineState> pipelineState = nil;
  CVMetalTextureCacheRef cacheTexture = nullptr;
  id<MTLSamplerState> samplerState = nil;
  CAMetalLayer *metalLayer = nullptr;  
  id<MTLTexture> outputTexture = nil;
  IOSurfaceRef ioSurface = nullptr;

protected:
  virtual void onSetSurface() override;
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual bool fetchFrame(ImageBuffer *imageBuffer) override;

public:
  virtual IRenderContext *getGpuContext() override;
  virtual ImageFormat getImageFormat() override;
  virtual IOSurfaceRef getIOSurface() override;

private:
  void createPipelineState();
  void createTextureCache();
  void closePipelineState();
  void closeTextureCache();

public:
  void updateNV12ToMetalLayer(CVImageBufferRef imageBuffer);

private:
  void logIOSurface();
};

}
