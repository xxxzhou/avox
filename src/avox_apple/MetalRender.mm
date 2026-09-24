#include "MetalRender.hpp"
#include "MetalWindow.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/video/ColorSpace.hpp"
#include <iostream>

#import <MetalKit/MetalKit.h>
#import <objc/runtime.h>
#if !TARGET_OS_IPHONE  // CVDisplayLink 是 macOS 专属
#import <CoreVideo/CVDisplayLink.h>
#endif
#include <mach/mach_time.h>
#include <TargetConditionals.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace avox {

// 与 shader 内 FragParams 同布局(20B), 每帧经 setFragmentBytes 下发
struct MetalFragParams {
  int hdrMode;
  int tenBit;
  int transfer;
  float peakNits;
  float sdrWhiteNits;
};

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

// 与 Dx11CSVideoRender 的 HLSL / glsl/yuv2rgbaV5.comp 同源的 HDR 链:
// PQ EOTF -> ACES(Narkowicz) -> BT.2020->BT.709 -> BT.709 OETF。
// 10bit x420 按 P010 布局高 10 位采样(R16/RG16 UNORM, k=65535/64/1023);
// 8bit NV12 承载 HDR(平台不支持 x420 回退)按 tv-range 展开同链处理
NSString *const nv12trgbBody = AVOX_SHADER_STRING(
    struct VertexIn {
      float2 position [[attribute(0)]];
      float2 texCoord [[attribute(1)]];
    };

    struct VertexOut {
      float4 position [[position]];
      float2 texCoord;
    };

    struct FragParams {
      int hdrMode;
      int tenBit;
      int transfer;
      float peakNits;
      float sdrWhiteNits;
    };

    vertex VertexOut vertexShader(const VertexIn in [[stage_in]]) {
      VertexOut out;
      out.position = float4(in.position, 0.0, 1.0);
      out.texCoord.x = in.texCoord.x;
      out.texCoord.y = 1.0f - in.texCoord.y;
      return out;
    }

    float3 pqToLinear(float3 n) {
      const float m1 = 0.1593017578125;
      const float m2 = 78.84375;
      const float c1 = 0.8359375;
      const float c2 = 18.8515625;
      const float c3 = 18.6875;
      float3 p = pow(clamp(n, float3(0.0), float3(1.0)), float3(1.0 / m2));
      float3 num = max(p - float3(c1), float3(0.0));
      return pow(num / (float3(c2) - p * float3(c3)), float3(1.0 / m1));
    }

    float3 hlgToLinear(float3 e) {
      float3 t = clamp(e, float3(0.0), float3(1.0));
      float3 lo = t * t / float3(3.0);
      float3 hi = (exp((t - float3(0.55991073)) / float3(0.17883277)) +
                   float3(0.28466892)) /
                  float3(12.0);
      float3 scene = mix(lo, hi, step(float3(0.5), t));
      float ys = dot(scene, float3(0.2627, 0.6780, 0.0593));
      return scene * pow(float3(max(ys, 1e-6)), float3(0.2));
    }

    float3 toneMap(float3 lin, float peakNits, float sdrWhite) {
      float xScale = 10000.0 / sdrWhite;
      float3 x = max(lin * xScale, float3(0.0));
      float3 a = (x * (x * 2.51 + 0.03)) / (x * (x * 2.43 + 0.59) + 0.14);
      float peakX = max(peakNits, sdrWhite) / sdrWhite;
      float peak =
          (peakX * (2.51 * peakX + 0.03)) / (peakX * (2.43 * peakX + 0.59) + 0.14);
      return clamp(a / peak, float3(0.0), float3(1.0));
    }

    float3 bt2020ToBt709(float3 c) {
      return max(float3(dot(c, float3(1.6605, -0.5876, -0.0728)),
                        dot(c, float3(-0.1246, 1.1329, -0.1006)),
                        dot(c, float3(-0.0182, -0.1006, 1.1187))),
                 float3(0.0));
    }

    float3 linearToBt709(float3 c) {
      float3 lo = c * 4.5;
      float3 hi = 1.099 * pow(max(c, float3(0.0)), float3(0.45)) - 0.099;
      return mix(lo, hi, step(float3(0.018), c));
    }

    float3 processColor(float3 rgb, constant FragParams& params) {
      if (params.hdrMode == 2) {
        return rgb;
      }
      if (params.transfer == 2) {
        float3 lin = pqToLinear(rgb);
        lin = toneMap(lin, params.peakNits, params.sdrWhiteNits);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
      }
      if (params.transfer == 3) {
        float3 lin = hlgToLinear(rgb) * 0.1;
        lin = toneMap(lin, params.peakNits, params.sdrWhiteNits);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
      }
      return rgb;
    }

    fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                   texture2d<float> yTexture [[texture(0)]],
                                   texture2d<float> uvTexture [[texture(1)]],
                                   sampler sampler [[sampler(0)]],
                                   constant FragParams& params [[buffer(0)]],
                                   // macOS 26 新 Metal 编译器拒绝 [[buffer(n)]]
                                   // 修饰数组参数("buffer attribute cannot be
                                   // applied to types", 2677 列实证), 改指针
                                   // 形式; shader 内 colorMat[i] 访问不变
                                   constant float* colorMat [[buffer(1)]]) {
      float y = yTexture.sample(sampler, in.texCoord).r;
      float2 uv = uvTexture.sample(sampler, in.texCoord).rg;
      float3 rgb;
      if (params.tenBit == 1) {
        float k = 65535.0 / 64.0 / 1023.0;
        float yy = (y * k - 64.0 / 1023.0) / (876.0 / 1023.0);
        float uu = (uv.x * k - 0.5) * (876.0 / 896.0);
        float vv = (uv.y * k - 0.5) * (876.0 / 896.0);
        rgb = float3(yy + 1.4746 * vv, yy - 0.164553 * uu - 0.571353 * vv,
                     yy + 1.8814 * uu);
      } else if (params.transfer == 2 || params.transfer == 3) {
        float yy = (y - 16.0 / 255.0) / (219.0 / 255.0);
        float uu = (uv.x - 128.0 / 255.0) / (224.0 / 255.0);
        float vv = (uv.y - 128.0 / 255.0) / (224.0 / 255.0);
        rgb = float3(yy + 1.4746 * vv, yy - 0.164553 * uu - 0.571353 * vv,
                     yy + 1.8814 * uu);
      } else {
        // 颜色矩阵与 Vulkan V1/V5 / Dx11 的 vec4(yuv,1)*colorMat 同源: 内存 slot k =
        // 输出通道 k 的系数, 末位是 range 偏移(limited 已含量程展开); 输入 y/uv 为 raw [0,1]
        float4 yuv = float4(y, uv, 1.0);
        rgb = float3(
          dot(yuv, float4(colorMat[0], colorMat[1], colorMat[2], colorMat[3])),
          dot(yuv, float4(colorMat[4], colorMat[5], colorMat[6], colorMat[7])),
          dot(yuv, float4(colorMat[8], colorMat[9], colorMat[10], colorMat[11])));
      }
      rgb = processColor(rgb, params);
      return float4(clamp(rgb, 0.0, 1.0), 1.0);
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

MetalRender::MetalRender() { renderType = RenderType::Metal; updateColorMat(); }

MetalRender::~MetalRender() { releaseGraph(); }

// 直通层配置: forceHDR 且过 EDR 探测时 16Float + BT.2100 PQ(PQ 值原样落帧),
// 否则恢复 SDR 默认。管线色附格式随 bF16Pipeline 对齐, 两者必须同帧一致,
// 否则 Metal 校验层断言(framebuffer 与 pipeline 格式必须一致, iOS 实测)
static void applyLayerHdrConfig(CAMetalLayer* layer, bool pass) {
  if (!layer) {
    return;
  }
  if (pass) {
    layer.wantsExtendedDynamicRangeContent = YES;
    if (@available(macOS 10.15, iOS 13.0, *)) {
      CGColorSpaceRef pqcs =
          CGColorSpaceCreateWithName(kCGColorSpaceITUR_2100_PQ);
      layer.colorspace = pqcs;
      if (pqcs) {
        CFRelease(pqcs);
      }
    }
    layer.pixelFormat = MTLPixelFormatRGBA16Float;
  } else {
    layer.wantsExtendedDynamicRangeContent = NO;
    layer.colorspace = nil;
    layer.pixelFormat = MTLPixelFormatRGBA8Unorm;
  }
  LOGFLF(LogLevel::info, "applyLayerHdrConfig pass:", pass ? 1 : 0,
         " pixelFormat:", (int)layer.pixelFormat,
         " hasColorspace:", layer.colorspace != nil ? 1 : 0);
}

void MetalRender::onSetSurface() {
  // NSView -> CAMetalLayer 的归一已上提到 getNativeSurface(见 Window.cpp), 到这里
  // surface 必是层(device 也兜底过), 下游各后端无需各自再判
  metalLayer = (CAMetalLayer *)surface;
  if (!metalLayer) {
    return;
  }
  // fetchFrame 从 drawable blit 取帧, framebufferOnly=YES 的 drawable 不能作 blit 源
  // -> screenShot 恒败; 直通快路未测出收益, 要它(并放弃抓帧)才设 AVOX_FB_ONLY=1
  static const bool bFbOnly = [] {
    const char* e = getenv("AVOX_FB_ONLY");
    return e && strcmp(e, "1") == 0;
  }();
  metalLayer.framebufferOnly = bFbOnly ? YES : NO;
  applyLayerHdrConfig(metalLayer, metalHdrPassthrough.load());
}

bool MetalRender::vaildAndInitGraph() {
  // 原子读+清重置标志: 释放决策用捕获值, 只清本次读到的值 —— 读-清分离期间宿主
  // 新置的请求不会被盲写抹掉, 留到下一帧再重建一次(见 VideoRender.hpp 契约)
  const bool bNeedReset = bResetFlag.exchange(false);
  // 如果没有窗口，但是大小变化了，需要重置
  if (!metalLayer && bNeedReset) {
    releaseGraph();
  }
  // 建不建由 pipelineState/cacheTexture 是否为空驱动, 不依赖本标志
  // 直通态翻转: 层与管线同帧切格式(RGBA8Unorm <-> RGBA16Float+PQ), 重建对齐
  const bool wantF16 = metalHdrPassthrough.load();
  if (wantF16 != bF16Pipeline) {
    LOGFLF(LogLevel::info, "hdr pipeline flip f16:", wantF16 ? 1 : 0);
    bF16Pipeline = wantF16;
    applyLayerHdrConfig(metalLayer, wantF16);
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
  renderCVPixelBuffer(imageBuffer);
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
  // x420 与 nv12 同为 biplanar, packed 视图约定一致(r8/r16 + height*3/2)
  bool bTenBit = pbType == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
                 pbType == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
  // 交付类型随真实 pb 走: x420 → p010, 否则 nv12。getCpuFrameBuffer 据此
  // 上报, 不再硬编码 —— 否则 10bit 硬解帧会被谎报成 nv12 (yuvout-h264-hi10p
  // 哨兵在 macOS 实证 type-mismatch: VideoToolbox 能解 High10, 帧是 P010)
  cpuPublishedType = bTenBit ? YuvType::p010 : YuvType::nv12;
  if (!bTenBit &&
      pbType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
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
    // nv12/x420 packed布局约定: r8/r16 + height*3/2 + rowPitch(字节),
    // UV平面紧随Y平面; VT biplanar平面间常有对齐间隙, 不连续时聚合成
    // 紧凑packed再交付(零拷贝只在天然连续时成立)
    uint8_t *yBase =
        (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0);
    uint8_t *uvBase =
        (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1);
    int32_t yPitch =
        (int32_t)CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
    int32_t uvPitch =
        (int32_t)CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);
    int32_t yHeight = (int32_t)CVPixelBufferGetHeight(imageBuffer);
    int32_t uvHeight = yHeight / 2;
    if (!yBase || !uvBase || yPitch <= 0 || uvPitch <= 0) {
      LOGFLF(LogLevel::warn, "cpu yuv out plane layout invalid, skip");
      CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
      CFRelease(imageBuffer);
      publishedTick = renderTick;
      return;
    }
    cpuPb = imageBuffer;
    ImageFormat fmt = {};
    fmt.width = (int32_t)CVPixelBufferGetWidth(imageBuffer);
    fmt.height = yHeight + uvHeight;
    fmt.imageType = bTenBit ? ImageType::r16 : ImageType::r8;
    fmt.rowPitch = yPitch;
    if (uvBase == yBase + (size_t)yPitch * yHeight) {
      cpuBuffer.setData(yBase, fmt, false);
    } else {
      size_t yBytes = (size_t)yPitch * yHeight;
      size_t uvBytes = (size_t)uvPitch * uvHeight;
      if (cpuPack.size() < yBytes + uvBytes) {
        cpuPack.resize(yBytes + uvBytes);
      }
      memcpy(cpuPack.data(), yBase, yBytes);
      memcpy(cpuPack.data() + yBytes, uvBase, uvBytes);
      cpuBuffer.setData(cpuPack.data(), fmt, false);
    }
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
  // 发布帧的真实类型 (publishCpuFrame 按 pbType 填写): VT 硬解 P010 上报
  // p010, 8bit 上报 nv12 —— 硬编码 nv12 曾把 10bit 帧谎报成 8bit
  yuvType = cpuPublishedType;
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
    // 运行时 MSL 编译失败只报一句 failed 无从排查(macOS 26 实证),
    // 把编译器诊断带上: 语法/类型错误都在 localizedDescription 里
    LOGFLF(LogLevel::warn, "failed to create library:",
           libraryError.localizedDescription
               ? libraryError.localizedDescription.UTF8String
               : "(no description)");
    return;
  }
  id<MTLFunction> vertexFunction =
      [library newFunctionWithName:@"vertexShader"];
  id<MTLFunction> fragmentFunction =
      [library newFunctionWithName:@"fragmentShader"];

  pipelineDescriptor.vertexFunction = vertexFunction;
  pipelineDescriptor.fragmentFunction = fragmentFunction;
  pipelineDescriptor.colorAttachments[0].pixelFormat =
      bF16Pipeline ? MTLPixelFormatRGBA16Float : MTLPixelFormatRGBA8Unorm;

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
    // Metal requires IOSurface texture bytesPerRow 16-byte alignment (odd widths like 854 would assert and crash), align to 64
    (id)kIOSurfaceBytesPerRow : @((imageFormat.width * 4 + 63) & ~63)
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

// 颜色/HDR参数: 每帧渲染时直接读取成员, 无需脏标记
void MetalRender::setColorSpace(const ColorSpaceDesc &c) {
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;
  }
  cs = c;
  updateColorMat();
}

void MetalRender::updateColorMat() {
  // buildYuvToRgb 行优先: row0/1/2 = R/G/B 输出通道系数(末列为 range 偏移), row3 恒固定
  Mat4x4f m = buildYuvToRgb(cs);
  colorMatData[0] = m.row0.x;  colorMatData[1] = m.row0.y;
  colorMatData[2] = m.row0.z;  colorMatData[3] = m.row0.w;
  colorMatData[4] = m.row1.x;  colorMatData[5] = m.row1.y;
  colorMatData[6] = m.row1.z;  colorMatData[7] = m.row1.w;
  colorMatData[8] = m.row2.x;  colorMatData[9] = m.row2.y;
  colorMatData[10] = m.row2.z; colorMatData[11] = m.row2.w;
  colorMatData[12] = m.row3.x; colorMatData[13] = m.row3.y;
  colorMatData[14] = m.row3.z; colorMatData[15] = m.row3.w;
}

void MetalRender::setHdrMeta(const HdrMeta &meta) {
  if (!meta.valid) {
    return;
  }
  hdrMeta = meta;
}

void MetalRender::setHdrMode(HdrMode mode) { hdrMode = mode; }

// ---- vsync 相位对齐(AVOX_VSYNC_ALIGN=0 关) ----
// 自由节拍 commit 相位随机: 实测提交间隔落在 ~25/50ms; 对齐后收敛到 33/50ms,
// 即 25fps@60Hz 的 2-2-3 固有节奏(2026-09-24 vsynctest 间隔直方图实测)。
#if !TARGET_OS_IPHONE
static CVDisplayLinkRef gVsyncLink = nullptr;
#endif
static std::atomic<int64_t> gLastVsyncNs{0};
static std::atomic<int64_t> gVsyncPeriodNs{0};
static std::once_flag gVsyncOnce;

static int64_t machNowNs() {
  static mach_timebase_info_data_t tb = [] {
    mach_timebase_info_data_t t{};
    mach_timebase_info(&t);
    return t;
  }();
  return (int64_t)(mach_absolute_time() * tb.numer / tb.denom);
}

static void ensureVsyncLink() {
#if TARGET_OS_IPHONE
  // iOS 无 CVDisplayLink: vsync 相位对齐为可选优化, 先退化自由节拍,
  // CADisplayLink 接入另案
#else
  std::call_once(gVsyncOnce, [] {
    if (CVDisplayLinkCreateWithActiveCGDisplays(&gVsyncLink) != kCVReturnSuccess ||
        !gVsyncLink) {
      return;
    }
    CVDisplayLinkSetOutputCallback(
        gVsyncLink,
        [](CVDisplayLinkRef link, const CVTimeStamp*, const CVTimeStamp*,
           CVOptionFlags, CVOptionFlags*, void*) -> CVReturn {
          double sec = CVDisplayLinkGetActualOutputVideoRefreshPeriod(link);
          if (sec > 0) {
            gVsyncPeriodNs.store((int64_t)(sec * 1e9), std::memory_order_relaxed);
          }
          gLastVsyncNs.store(machNowNs(), std::memory_order_relaxed);
          return kCVReturnSuccess;
        },
        nullptr);
    CVDisplayLinkStart(gVsyncLink);
  });
#endif
}

// 唤醒点取在 vsync 边界前 3ms: 睡到边界本身会被 sleep_for 过冲推过界, commit 归到
// 下一边界白等一周期(实测相位 +2.0ms -> +13.1ms, 33ms:50ms 由 1.6:1 升到 2.0:1)
static void waitVsyncPhase() {
  const int64_t period = gVsyncPeriodNs.load(std::memory_order_relaxed);
  const int64_t last = gLastVsyncNs.load(std::memory_order_relaxed);
  if (period <= 0 || last <= 0) {
    return;
  }
  const int64_t kLeadNs = 3000000;
  const int64_t nowNs = machNowNs();
  int64_t wake = last + ((nowNs - last) / period + 1) * period - kLeadNs;
  if (wake <= nowNs) {  // 提前量已被吃光: 顺延一个周期
    wake += period;
  }
  const int64_t waitNs = wake - nowNs;
  // 保险丝: 等待超过 2 个周期说明计算异常, 放弃本次对齐照常绘制
  if (waitNs > 2 * period) {
    return;
  }
  if (waitNs > 0) {
    // mach_wait_until 在渲染线程实测会一睡不返, 用 chrono 相对睡代替
    std::this_thread::sleep_for(std::chrono::nanoseconds(waitNs));
  }
}

void MetalRender::renderCVPixelBuffer(CVImageBufferRef imageBuffer) {
  // 异步渲染任务堆积或自动释放池（Autorelease Pool）未及时清理
  // CVMetalTextureCacheCreateTextureFromImage 内部以及 Metal
  // 的一些方法会产生大量 autorelease 对象。
  // 如果你的这段代码是在一个高频循环（如 while 或
  // CADisplayLink）中执行，且没有手动包裹 @autoreleasepool 这些对象只有在主线程
  // RunLoop 结束时才会释放。
  @autoreleasepool {
    static const bool bVsyncAlign = [] {
      const char* e = getenv("AVOX_VSYNC_ALIGN");
      return !e || strcmp(e, "0") != 0;
    }();
    if (metalLayer && bVsyncAlign) {
      ensureVsyncLink();
      waitVsyncPhase();
    }
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
    // x420(Main10)平面为16bit字高位对齐10bit(P010布局), 采样走 R16/RG16 UNORM
    OSType pbType = CVPixelBufferGetPixelFormatType(imageBuffer);
    bool bTenBit = pbType == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
                   pbType == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    // 创建 Y 平面纹理
    CVReturn err = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cacheTexture, imageBuffer, nullptr,
        bTenBit ? MTLPixelFormatR16Unorm : MTLPixelFormatR8Unorm, width, height,
        0, &yTextureRef);
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
        bTenBit ? MTLPixelFormatRG16Unorm : MTLPixelFormatRG8Unorm, width / 2,
        height / 2, 1, &uvTextureRef);
    if (err == kCVReturnSuccess) {
      uvTexture = CVMetalTextureGetTexture(uvTextureRef);
      CFRelease(uvTextureRef);
    } else {
      LOGFLF(LogLevel::warn, "failed to create uv texture");
      return;
    }
    id<MTLCommandBuffer> commandBuffer = [getCommandQueue() commandBuffer];
    // 自适应长宽: 非全屏时按视频比例居中 letterbox(与 Dx11Window::onTickWin
    // 同用 getViewRect), 黑边来自整附件 Clear; 离屏 outputTexture 是原帧
    // 尺寸的输出/抓帧目标, 不裁剪
    MTLViewport viewport = {0, 0, (double)targetTexture.width,
                            (double)targetTexture.height, 0.0, 1.0};
    if (metalLayer && !bFullScreen && aspect > 0.0f) {
      const vec4i viewRect = getViewRect((int)targetTexture.width,
                                         (int)targetTexture.height, aspect);
      viewport = {(double)viewRect.x, (double)viewRect.y,
                  (double)viewRect.z, (double)viewRect.w, 0.0, 1.0};
    }
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
    [commandEncoder setViewport:viewport];
    // 绑定采样器状态到索引 0 的采样器位置
    [commandEncoder setFragmentSamplerState:samplerState atIndex:0];
    // 设置顶点缓冲区
    [commandEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
    // 颜色/HDR参数随帧下发(20B, 免常量缓冲与脏标记)
    MetalFragParams params = {};
    params.hdrMode = (int)hdrMode;
    params.tenBit = bTenBit ? 1 : 0;
    params.transfer = (int)cs.transfer;
    params.peakNits = (float)hdrPeakNits(hdrMeta);
    params.sdrWhiteNits = 100.0f;
    [commandEncoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
    // 颜色矩阵(行优先 16 浮点): 替换 shader 旧硬编码 BT.601, 尊重 cs.standard/range
    [commandEncoder setFragmentBytes:colorMatData length:sizeof(colorMatData) atIndex:1];
    // 设置纹理
    [commandEncoder setFragmentTexture:yTexture atIndex:0];
    [commandEncoder setFragmentTexture:uvTexture atIndex:1];
    // 绘制
    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6];
    [commandEncoder endEncoding];
    // 渲染线程无 RunLoop, 隐式 CA 事务可能不下刷; 显式事务逐帧 flush。
    // 2026-09-24 实测未观测到差异(present 节奏本就是干净 25fps), 属理论保险
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    // 仅在metalLayer存在时presentDrawable
    if (metalLayer && drawable) {
      [commandBuffer presentDrawable:drawable];
    }
    [commandBuffer commit];
    [CATransaction commit];
    // 实验观测: commit 时刻相对 vsync 边界的相位(对齐后应聚集在 0~几 ms)
    if (metalLayer) {
      static const bool bPlog = [] {
        const char* e = getenv("AVOX_PRESENT_LOG");
        return e && *e == '1';
      }();
      if (bPlog) {
        const int64_t periodNs =
            gVsyncPeriodNs.load(std::memory_order_relaxed);
        const int64_t lastNs = gLastVsyncNs.load(std::memory_order_relaxed);
        const int64_t now = machNowNs();
        const int64_t phase =
            periodNs > 0 && lastNs > 0 ? (now - lastNs) % periodNs : -1;
        fprintf(stderr, "PRESENT ns=%lld phase=%lld period=%lld drawable=%p\n",
                (long long)now, (long long)phase, (long long)periodNs,
                (__bridge void*)drawable);
      }
    }
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
