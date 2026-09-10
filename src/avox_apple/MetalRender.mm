#include "MetalRender.hpp"
#include "MetalWindow.hpp"
#include "avox/module/AvoxManager.hpp"
#include <iostream>

// 若有使用 MetalKit 相关功能，也可包含该头文件
#import <MetalKit/MetalKit.h>

namespace avox {

// 顶点数据
const float vertices[] = {
    // 第一个三角形
    -1.0f, -1.0f, 0.0f, 0.0f, // 左下角
    1.0f, -1.0f, 1.0f, 0.0f,  // 右下角
    -1.0f, 1.0f, 0.0f, 1.0f,  // 左上角

    // 第二个三角形
    1.0f, -1.0f, 1.0f, 0.0f, // 右下角
    1.0f, 1.0f, 1.0f, 1.0f,  // 右上角
    -1.0f, 1.0f, 0.0f, 1.0f  // 左上角
};

// 单独处理 #include 指令
NSString *const nv12trgbPrefix =
    @"#include <metal_stdlib>\nusing namespace metal;\n";

NSString *const nv12trgbBody = AVOX_SHADER_STRING(
    struct VertexIn {
      float2 position [[attribute(0)]];
      float2 texCoord [[attribute(1)]];
    };

    struct VertexOut {
      float4 position [[position]];
      float2 texCoord;
    };

    vertex VertexOut vertexShader(const VertexIn in [[stage_in]]) {
      VertexOut out;
      out.position = float4(in.position, 0.0, 1.0);
      out.texCoord.x = in.texCoord.x;
      out.texCoord.y = 1.0f - in.texCoord.y;
      return out;
    }

    fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                   texture2d<float> yTexture [[texture(0)]],
                                   texture2d<float> uvTexture [[texture(1)]],
                                   sampler sampler [[sampler(0)]]) {
      // 采样 Y 分量
      float y = yTexture.sample(sampler, in.texCoord).r;
      float2 uv = uvTexture.sample(sampler, in.texCoord).rg;

      // 调整 YUV 分量到标准范围
      const float yOffset = 16.0 / 255.0;
      const float uvOffset = 128.0 / 255.0;
      float3 yuv = float3(y - yOffset, uv - uvOffset);

      // 使用 BT.601 标准的 YUV 转 RGB 矩阵
      float3x3 conversionMatrix = float3x3(1.164, 0.000, 1.596, 1.164, -0.392,
                                           -0.813, 1.164, 2.017, 0.000);
      float3 rgb = yuv * conversionMatrix;

      // 限制 RGB 分量在 [0, 1] 范围内
      rgb = clamp(rgb, 0.0, 1.0);
      // return float4(rgb.r, rgb.g, rgb.b, 1.0);
      return float4(rgb, 1.0);
    });

NSString *const nv12trgb =
    [NSString stringWithFormat:@"%@%@", nv12trgbPrefix, nv12trgbBody];

void regIOSVRender() {
  RegFunc metalRenderReg = {
      "metal render init", []() {
        VRenderDesc renderDesc = {};
        renderDesc.name = "Metal Render";
        AvoxManager::Get().vRender.regInitFunc(
            RenderType::Metal, renderDesc,
            []() -> VideoRender * { return new MetalRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(metalRenderReg);
}

MetalRender::MetalRender() { renderType = RenderType::Metal; }

MetalRender::~MetalRender() { releaseGraph(); }

void MetalRender::onSetSurface() {
  metalLayer = surface;
  // CAMetalLayer 默认 framebufferOnly=YES, drawable 纹理不能当 blit 源,
  // 抓帧(screenShot)必须关掉; 代价是放弃部分合成器优化
  if (metalLayer) {
    metalLayer.framebufferOnly = NO;
    // CAMetalLayer 默认 BGRA8Unorm, 而渲染管线(255行)固定 RGBA8Unorm,
    // 不对齐会被 Metal 校验层断言(iOS 实测): framebuffer 与 pipeline 格式必须一致
    metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm;
  }
}

bool MetalRender::vaildAndInitGraph() {
  // 如果没有窗口，但是大小变化了，需要重置
  if (!metalLayer && bResetFlag) {
    releaseGraph();
  }
  if (pipelineState != nil && cacheTexture != nil) {
    return true;
  }
  initContext();
  createPipelineState();
  createTextureCache();
  return pipelineState != nil && cacheTexture != nil;
}

void MetalRender::releaseGraph() {
  closePipelineState();
  closeTextureCache();
  // 抓帧引用的是 drawable 纹理, 关闭时立刻放开, 不跨窗口生命周期持有
  lastTargetTexture = nil;
  // 释放回读资源,锁定随解锁一并放开
  if (cpuPb) {
    CVPixelBufferUnlockBaseAddress(cpuPb, kCVPixelBufferLock_ReadOnly);
    CFRelease(cpuPb);
    cpuPb = nullptr;
    bCpuPublished = false;
  }
  unInit();
}

IRenderContext *MetalRender::getGpuContext() { return this; }

ImageFormat MetalRender::getImageFormat() { return imageFormat; }

IOSurfaceRef MetalRender::getIOSurface() { return ioSurface; }

void MetalRender::renderGpuFrame(const GpuFrame &frame) {
  CVImageBufferRef imageBuffer = (CVImageBufferRef)frame.buffer;
  updateNV12ToMetalLayer(imageBuffer);
  // bOutCpuYuv时在buffer还存活的地方锁定发布(releaseGpuFrame随后CVBufferRelease,
  // 惰性回读会拿到已释放的buffer)
  if (bOutCpuYuv && !cpuIn) {
    publishCpuFrame(imageBuffer);
  }
  // 放到VideoRender::renderFrame中释放,不太好处理,后面再想下
  // 主要是有二种方式,一种是队列数据,一种是
  // CFRelease(imageBuffer);
}

void MetalRender::publishCpuFrame(CVImageBufferRef imageBuffer) {
  if (!imageBuffer || publishedTick == renderTick) {
    return;
  }
  OSType pbType = CVPixelBufferGetPixelFormatType(imageBuffer);
  if (pbType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
      pbType != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
    LOGFLF(LogLevel::warn, "cpu yuv out not support pixel format:",
           (int32_t)pbType);
    publishedTick = renderTick;
    return;
  }
  if (cpuPb != imageBuffer) {
    if (cpuPb) {
      CVPixelBufferUnlockBaseAddress(cpuPb, kCVPixelBufferLock_ReadOnly);
      CFRelease(cpuPb);
      cpuPb = nullptr;
    }
    CFRetain(imageBuffer);
    if (CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly) !=
        kCVReturnSuccess) {
      LOGFLF(LogLevel::warn, "lock pixel buffer failed");
      CFRelease(imageBuffer);
      publishedTick = renderTick;
      return;
    }
    cpuPb = imageBuffer;
    // nv12 packed布局约定: r8 + height*3/2 + rowPitch(字节),
    // biplanar连续内存,UV平面紧跟Y平面
    ImageFormat fmt = {};
    fmt.width = (int32_t)CVPixelBufferGetWidth(imageBuffer);
    fmt.height = (int32_t)CVPixelBufferGetHeight(imageBuffer) * 3 / 2;
    fmt.imageType = ImageType::r8;
    fmt.rowPitch = (int32_t)CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
    cpuBuffer.setData(
        (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0), fmt,
        false);
    bCpuPublished = true;
  }
  publishedTick = renderTick;
}

bool MetalRender::getCpuFrameBuffer(IImageBuffer **buffer, YuvType &yuvType,
                                    int64_t *pts) {
  // CPU输入(软解)不经过GPU,交基类packed视图
  if (cpuIn) {
    return VideoRender::getCpuFrameBuffer(buffer, yuvType, pts);
  }
  if (!bOutCpuYuv || !bCpuPublished || publishedTick != renderTick) {
    return false;
  }
  *buffer = &cpuBuffer;
  yuvType = YuvType::nv12;
  if (pts) {
    *pts = gpuFrame.pts;
  }
  return true;
}

bool MetalRender::fetchFrame(ImageBuffer *imageBuffer) {
  // 有 layer 时读上一帧画过的 drawable 纹理: 这里再 nextDrawable 拿到的是一张
  // 全新未绘制的 drawable(抓出来是清屏色), 而且取了不 present 会占空池子
  id<MTLTexture> targetTexture = metalLayer ? lastTargetTexture : outputTexture;
  // 检查目标纹理是否有效
  if (!targetTexture) {
    LOGFLF(LogLevel::warn, "targetTexture is invalid");
    return false;
  }
  // 直接从targetTexture获取尺寸和格式信息
  ImageFormat format = {};
  format.width = (int32_t)[targetTexture width];
  format.height = (int32_t)[targetTexture height];
  // 在fetchFrame函数中，可以这样获取实际的rowpitch
  // NSUInteger bytesPerRow = [targetTexture bytesPerRow];
  // if (bytesPerRow == 0) {
  //   // 如果bytesPerRow为0，使用默认计算方式
  //   bytesPerRow = format.width * 4;
  // }
  // format.rowPitch = bytesPerRow;
  // metalLayer与ioSurface我们都设rgba
  format.imageType = ImageType::rgba8;
  // 检查尺寸是否有效
  if (format.width == 0 || format.height == 0) {
    LOGFLF(LogLevel::warn, "targetTexture dimensions are invalid");
    return false;
  }
  // 申请format需要的内存
  imageBuffer->setImageFormat(format);
  // 创建命令缓冲区和命令编码器
  id<MTLCommandBuffer> commandBuffer = [getCommandQueue() commandBuffer];
  if (!commandBuffer) {
    LOGFLF(LogLevel::warn, "failed to create command buffer");
    return false;
  }
  // 创建临时纹理用于读取数据
  MTLTextureDescriptor *textureDescriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:format.width
                                  height:format.height
                               mipmapped:NO];
  textureDescriptor.usage =
      MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  textureDescriptor.storageMode = MTLStorageModeShared;
  id<MTLTexture> readTexture =
      [device newTextureWithDescriptor:textureDescriptor];
  if (!readTexture) {
    LOGFLF(LogLevel::warn, "failed to create read texture");
    return false;
  }
  // 使用blit命令编码器复制纹理数据
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  if (!blitEncoder) {
    LOGFLF(LogLevel::warn, "failed to create blit encoder");
    return false;
  }
  // 复制目标纹理到可读纹理
  [blitEncoder copyFromTexture:targetTexture
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(format.width, format.height, 1)
                     toTexture:readTexture
              destinationSlice:0
              destinationLevel:0
             destinationOrigin:MTLOriginMake(0, 0, 0)];
  [blitEncoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  // 从纹理读取数据到imageBuffer
  // rowPitch 约定 0 = 紧凑(宽*像素), 但 getBytes 的 bytesPerRow 传 0 是无效参数,
  // Metal 只会写进第一行, 抓出来整张几乎全 0(看图器显示成白/透明), 必须显式算
  const int32_t bytesPerRow =
      format.rowPitch > 0 ? format.rowPitch : format.width * 4;
  MTLRegion region = MTLRegionMake2D(0, 0, format.width, format.height);
  [readTexture getBytes:imageBuffer->getPointer()
            bytesPerRow:bytesPerRow
             fromRegion:region
            mipmapLevel:0];
  return true;
}

void MetalRender::createPipelineState() {
  // 创建渲染管线描述符
  MTLRenderPipelineDescriptor *pipelineDescriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  // 创建顶点描述符
  MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
  // 配置顶点属性 0: position
  vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[0].offset = 0;
  vertexDescriptor.attributes[0].bufferIndex = 0;
  // 配置顶点属性 1: texCoord 偏移 2 个 float 的大小
  vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[1].offset = 2 * sizeof(float);
  vertexDescriptor.attributes[1].bufferIndex = 0;
  // 配置顶点缓冲区布局
  // 每个顶点包含 4 个 float (2 个 position + 2 个 texCoord)
  vertexDescriptor.layouts[0].stride = 4 * sizeof(float);
  vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  vertexDescriptor.layouts[0].stepRate = 1;

  // 将顶点描述符关联到渲染管线描述符
  pipelineDescriptor.vertexDescriptor = vertexDescriptor;

  NSError *libraryError = nil;
  id<MTLLibrary> library = [device newLibraryWithSource:nv12trgb
                                                options:nil
                                                  error:&libraryError];
  if (!library) {
    LOGFLF(LogLevel::warn, "failed to create library");
    return;
  }
  id<MTLFunction> vertexFunction =
      [library newFunctionWithName:@"vertexShader"];
  id<MTLFunction> fragmentFunction =
      [library newFunctionWithName:@"fragmentShader"];

  pipelineDescriptor.vertexFunction = vertexFunction;
  pipelineDescriptor.fragmentFunction = fragmentFunction;
  pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

  NSError *pipelineError = nil;
  pipelineState =
      [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                             error:&pipelineError];
  if (!pipelineState) {
    LOGFLF(LogLevel::warn, "failed to create pipeline state");
  }
  // 创建采样器状态
  MTLSamplerDescriptor *samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
  samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
  samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
  samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
  samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerDescriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];
}

void MetalRender::createTextureCache() {
  CVReturn err = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                           nullptr, &cacheTexture);
  if (err != kCVReturnSuccess) {
    LOGFLF(LogLevel::warn, "failed to create texture cache");
  }
  CVMetalTextureCacheFlush(cacheTexture, 0);
  // 创建 IOSurface 属性字典
  NSDictionary *surfaceProps = @{
    (id)kIOSurfaceWidth : @(imageFormat.width),
    (id)kIOSurfaceHeight : @(imageFormat.height),
    (id)kIOSurfacePixelFormat : @(kCVPixelFormatType_32RGBA),
    (id)kIOSurfaceBytesPerElement : @(4),
    (id)kIOSurfaceBytesPerRow : @(imageFormat.width * 4)
  };
  ioSurface = IOSurfaceCreate((CFDictionaryRef)surfaceProps);
  MTLTextureDescriptor *textureDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:imageFormat.width
                                  height:imageFormat.height
                               mipmapped:NO];
  textureDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  textureDesc.storageMode = MTLStorageModeShared;
  outputTexture = [device newTextureWithDescriptor:textureDesc
                                         iosurface:ioSurface
                                             plane:0];
  LOGFLF(LogLevel::info, "ioSurface:", ioSurface,
         " output texture:", outputTexture);
}

void MetalRender::closePipelineState() { pipelineState = nil; }

void MetalRender::closeTextureCache() {
  if (cacheTexture) {
    CVMetalTextureCacheFlush(cacheTexture, 0);
    CFRelease(cacheTexture);
    cacheTexture = nullptr;
  }
  if (ioSurface) {
    CFRelease(ioSurface);
    ioSurface = nullptr;
    outputTexture = nil;
  }
}

void MetalRender::updateNV12ToMetalLayer(CVImageBufferRef imageBuffer) {
  // 异步渲染任务堆积或自动释放池（Autorelease Pool）未及时清理
  // CVMetalTextureCacheCreateTextureFromImage 内部以及 Metal
  // 的一些方法会产生大量 autorelease 对象。
  // 如果你的这段代码是在一个高频循环（如 while 或
  // CADisplayLink）中执行，且没有手动包裹 @autoreleasepool 这些对象只有在主线程
  // RunLoop 结束时才会释放。
  @autoreleasepool {
    id<CAMetalDrawable> drawable = nil;
    id<MTLTexture> targetTexture = nil;
    // 修改：根据metalLayer存在情况选择渲染目标
    if (metalLayer) {
      drawable = [metalLayer nextDrawable];
      targetTexture = drawable.texture;
    } else {
      targetTexture = outputTexture;
    }
    if (!imageBuffer || !cacheTexture || !pipelineState || !targetTexture) {
      LOGFLF(LogLevel::warn, "Invalid parameters for rendering");
      return;
    }
    id<MTLTexture> yTexture = nil;
    id<MTLTexture> uvTexture = nil;
    CVMetalTextureRef yTextureRef = nullptr;
    CVMetalTextureRef uvTextureRef = nullptr;
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    // 创建 Y 平面纹理
    CVReturn err = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cacheTexture, imageBuffer, nullptr,
        MTLPixelFormatR8Unorm, width, height, 0, &yTextureRef);
    if (err == kCVReturnSuccess) {
      yTexture = CVMetalTextureGetTexture(yTextureRef);
      CFRelease(yTextureRef);
    } else {
      LOGFLF(LogLevel::warn, "failed to create y texture");
      return;
    }
    // 创建 UV 平面纹理
    err = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cacheTexture, imageBuffer, nullptr,
        MTLPixelFormatRG8Unorm, width / 2, height / 2, 1, &uvTextureRef);
    if (err == kCVReturnSuccess) {
      uvTexture = CVMetalTextureGetTexture(uvTextureRef);
      CFRelease(uvTextureRef);
    } else {
      LOGFLF(LogLevel::warn, "failed to create uv texture");
      return;
    }
    id<MTLCommandBuffer> commandBuffer = [getCommandQueue() commandBuffer];
    MTLRenderPassDescriptor *renderPassDescriptor =
        [MTLRenderPassDescriptor renderPassDescriptor];
    renderPassDescriptor.colorAttachments[0].texture = targetTexture;
    renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    renderPassDescriptor.colorAttachments[0].clearColor =
        MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> commandEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [commandEncoder setRenderPipelineState:pipelineState];
    // 绑定采样器状态到索引 0 的采样器位置
    [commandEncoder setFragmentSamplerState:samplerState atIndex:0];
    // 设置顶点缓冲区
    [commandEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
    // 设置纹理
    [commandEncoder setFragmentTexture:yTexture atIndex:0];
    [commandEncoder setFragmentTexture:uvTexture atIndex:1];
    // 绘制
    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6];
    [commandEncoder endEncoding];
    // 仅在metalLayer存在时presentDrawable
    if (metalLayer && drawable) {
      [commandBuffer presentDrawable:drawable];
    }
    [commandBuffer commit];
    // 记下这一帧的目标纹理供 checkShot->fetchFrame 抓帧(同队列, 顺序有保证)
    lastTargetTexture = targetTexture;
  }
  // logIOSurface();
}

void MetalRender::logIOSurface() {
  if (ioSurface) {
    // 验证ioSurface是否成功写入数据,读取ioSurface里数据
    IOSurfaceLock(ioSurface, kIOSurfaceLockReadOnly, nil);
    void *baseAddress = IOSurfaceGetBaseAddress(ioSurface);
    uint8_t *data = (uint8_t *)baseAddress + 21300;
    AvoxData avoxData = {};
    avoxData.data = data;
    avoxData.size = 100;
    log(LogLevel::info, "ioSurface data:", avoxData);
    IOSurfaceUnlock(ioSurface, kIOSurfaceLockReadOnly, nil);
  }
}

}
