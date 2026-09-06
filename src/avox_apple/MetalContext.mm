#include "MetalContext.hpp"
#include <iostream>

namespace avox {

MetalContext::MetalContext() {}

MetalContext::~MetalContext() { unInit(); }

void MetalContext::initContext() {
  device = MTLCreateSystemDefaultDevice();
  commandQueue = [device newCommandQueue];
}

//void MetalContext::updateImageFormat(ImageFormat format) {
//  imageFormat = format;
//  onImageFormatChange();
//}

void MetalContext::unInit() {
  device = nil;
  commandQueue = nil;
}

id<MTLDevice> MetalContext::getDevice() { return device; }

id<MTLCommandQueue> MetalContext::getCommandQueue() { return commandQueue; }

}
