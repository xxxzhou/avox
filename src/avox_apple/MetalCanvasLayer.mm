#import "MetalCanvasLayer.hpp"

namespace avox {

void MetalCanvasLayer::updateCanvas(const AssCanvas& canvas) {
  state.updateCanvas(canvas);
}

void MetalCanvasLayer::clearCanvas() { state.clearCanvas(); }

void MetalCanvasLayer::setCanvasTransform(float scale, float offsetX,
                                          float offsetY, float opacity) {
  state.setCanvasTransform(scale, offsetX, offsetY, opacity);
}

bool MetalCanvasLayer::ensureTexture(id<MTLDevice> d, int32_t frameW,
                                     int32_t frameH) {
  if (!d || frameW <= 0 || frameH <= 0) {
    return false;
  }
  // 画布 = 合成画布尺寸(>1080p 恒 1920x1080 基准), 铺满全帧由 sampler
  // 归一化映射拉伸(与 VK 同机制)
  int32_t cw = 0;
  int32_t ch = 0;
  subtitleCanvasSize(frameW, frameH, &cw, &ch);
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  if (canvasTexture && (int32_t)canvasTexture.width == cw &&
      (int32_t)canvasTexture.height == ch) {
    return true;
  }
  if (!state.reset(cw, ch)) {
    return false;
  }
  MTLTextureDescriptor* desc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:(NSUInteger)cw
                                  height:(NSUInteger)ch
                               mipmapped:NO];
  desc.usage = MTLTextureUsageShaderRead;
  canvasTexture = [d newTextureWithDescriptor:desc];
  // 建图前来件暂存回灌(避免丢第一屏字幕, 与 VK applyPending 同流程)
  if (canvasTexture && state.hasPending()) {
    std::vector<uint8_t> buf;
    int32_t w = 0, h = 0, stride = 0, x = 0, y = 0;
    state.takePending(buf, w, h, stride, x, y);
    state.updateCanvas(AssCanvas{buf.data(), w, h, stride, x, y});
  }
  return canvasTexture != nil;
}

void MetalCanvasLayer::uploadIfNeeded() {
  if (!canvasTexture || !state.takeUploadDue()) {
    return;
  }
  // 整画布上传(bytesPerRow=画布宽*4): 内容变化才传, 对白节奏数秒一次
  [canvasTexture
      replaceRegion:MTLRegionMake2D(0, 0, canvasTexture.width,
                                    canvasTexture.height)
        mipmapLevel:0
          withBytes:state.data()
        bytesPerRow:canvasTexture.width * 4];
}

}
