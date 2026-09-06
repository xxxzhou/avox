#pragma once

#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>

#include "IOSCommon.hpp"
#include "avox/AvoxLayer.h"

namespace avox {

class MetalContext : public IRenderContext {
public:
  MetalContext();
  virtual ~MetalContext();

protected:
  // ImageFormat imageFormat = {};

protected:
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> commandQueue = nil;

public:
  void initContext();
  // void updateImageFormat(ImageFormat format);
  void unInit();

protected:
  virtual void onImageFormatChange() {};

public:
  virtual RenderType getRenderType() override { return RenderType::Metal; }

public:
  id<MTLDevice> getDevice();
  id<MTLCommandQueue> getCommandQueue();

public:
  virtual ImageFormat getImageFormat() = 0;
  virtual IOSurfaceRef getIOSurface() = 0;
};

}
