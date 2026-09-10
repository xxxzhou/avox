#include "VkVideoRender.hpp"

#include "avox/AvoxVideo.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include "avox/video/ImageBuffer.hpp"
#include "avox/video/WindowRender.hpp"
#include "../geometry/VkGeometryLayer.hpp"

namespace avox {

void regVkRender() {
  RegFunc regFunc = {
      "vk render init", []() {
        VRenderDesc vkRenderDesc = {};
        vkRenderDesc.name = "Vk Render";
        AvoxManager::Get().vRender.regInitFunc(
            RenderType::Vulkan, vkRenderDesc,
            []() -> VideoRender* { return new VkVideoRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

VkVideoRender::VkVideoRender() {
  renderType = RenderType::Vulkan;
  lutIndex = 0;
#ifdef AVOX_ENABLE_FREETYPE
  fontRender = std::make_unique<FontRender>();
#endif
  geometryRender = std::make_unique<GeometryRender>();
}

VkVideoRender::~VkVideoRender() { releaseGraph(); }

IRenderContext* VkVideoRender::getGpuContext() { return nullptr; }

void VkVideoRender::enableWatermark(const Watermark& paramet,
                                    IImageBuffer* imageBuffer) {
  bEnableBlend = true;
  if (!blendImage) {
    blendImage = std::make_unique<ImageBuffer>();
  }
  blendImage->copyFrom(imageBuffer, true);
  blendParamet.centerX = paramet.centerX;
  blendParamet.centerY = paramet.centerY;
  blendParamet.width = paramet.width;
  blendParamet.height = paramet.height;
  blendParamet.alaph = paramet.alaph;
  bResetFlag = true;
}
void VkVideoRender::disableWatermark() {
  bEnableBlend = false;
  bResetFlag = true;
}

// Lut
void VkVideoRender::enableLut(const LutParamet& paramet) {
  if (lutIndex == paramet.lutIndex) {
    return;
  }
  lutIndex = paramet.lutIndex;
  bResetFlag = true;
}

void VkVideoRender::disableLut() {
  if (lutIndex == 0) {
    return;
  }
  lutIndex = 0;
  bResetFlag = true;
}

// 基础图像调整(色调/亮度/对比度/饱和度/伽玛) - 一次传入整组参数
void VkVideoRender::enableBasicAdjust(const BasicAdjustParamet& paramet) {
  basicAdjustValue = paramet;
  if (!bBasicAdjust) {
    bBasicAdjust = true;
    bResetFlag = true;
  }
}

// 关闭整组基础调整, 全部复位中性
void VkVideoRender::disableBasicAdjust() {
  if (bBasicAdjust) {
    bBasicAdjust = false;
    basicAdjustValue = {};
    bResetFlag = true;
  }
}

// 锐度
void VkVideoRender::updateSharpen(const SharpenVideo& paramet) {
  if (!bSharpen) {
    bSharpen = true;
    bResetFlag = true;
  }
  sharpenParamet = paramet;
}

void VkVideoRender::disableSharpen() {
  if (bSharpen) {
    bSharpen = false;
    bResetFlag = true;
  }
}

// 颜色空间: 仅在变化时下发, 运行时重传矩阵, 不重建 graph
void VkVideoRender::setColorSpace(const ColorSpaceDesc& c) {
  if (c.standard == colorSpace.standard && c.range == colorSpace.range) {
    return;
  }
  colorSpace = c;
  if (rgba2YUV) {
    rgba2YUV->get()->setColorSpace(c);
  }
  if (yuv2RGBA) {
    yuv2RGBA->get()->setColorSpace(c);
  }
}

#ifdef AVOX_ENABLE_FREETYPE
FontRender* VkVideoRender::enableRenderFont() {
  fontRender->setEnable(true);
  bResetFlag = true;
  return fontRender.get();
}

void VkVideoRender::disableRenderFont() {
  fontRender->setEnable(false);
  fontRender->setFontLayer(nullptr);
  bResetFlag = true;
}
#endif

GeometryRender* VkVideoRender::enableRenderGeometry() {
  geometryRender->setEnable(true);
  bResetFlag = true;
  return geometryRender.get();
}

void VkVideoRender::disableRenderGeometry() {
  // 对象常驻，仅让 VkGeometryLayer 在下次 graph 重建时不接入执行链
  geometryRender->setEnable(false);
  geometryRender->setLayer(nullptr);
  bResetFlag = true;
}

void VkVideoRender::enableSizeChange(int32_t width, int32_t height) {
  bUseNewSize = true;
  userWidth = width;
  userHeight = height;
  bResetFlag = true;
}

void VkVideoRender::enableSizeScale(float scale) {
  bUseNewSize = true;
  sizeScale = scale;
  bResetFlag = true;
}

void VkVideoRender::disableSizeChange() {
  bUseNewSize = false;
  bResetFlag = true;
}

void VkVideoRender::enableAnime4K(const Anime4KParamet& paramet) {
  anime4KParamet = paramet;
  bEnableAnime4K = true;
  // 互斥: 开启Anime4K时关闭QualityEnhance和FSR
  bEnableQualityEnhance = false;
  bEnableFSR = false;
  bResetFlag = true;
}

void VkVideoRender::disableAnime4K() {
  bEnableAnime4K = false;
  bResetFlag = true;
}

void VkVideoRender::enableQualityEnhance(const QualityEnhanceParamet& paramet) {
  qualityEnhanceParamet = paramet;
  bEnableQualityEnhance = true;
  // 互斥: 开启画质增强时关闭Anime4K和FSR
  bEnableAnime4K = false;
  bEnableFSR = false;
  bResetFlag = true;
}

void VkVideoRender::disableQualityEnhance() {
  bEnableQualityEnhance = false;
  bResetFlag = true;
}

void VkVideoRender::enableFSR(const FSRParamet& paramet) {
  fsrParamet = paramet;
  bEnableFSR = true;
  // 互斥: 开启FSR时关闭Anime4K和QualityEnhance
  bEnableAnime4K = false;
  bEnableQualityEnhance = false;
  bResetFlag = true;
}

void VkVideoRender::disableFSR() {
  bEnableFSR = false;
  bResetFlag = true;
}

vec2i VkVideoRender::getOutSize() {
  vec2i size = {imageFormat.width, imageFormat.height};
  if (bUseNewSize) {
    size = {resizeParamet.newWidth, resizeParamet.newHeight};
  }
  return size;
}

void VkVideoRender::onParametUpdate() {
  if (basicAdjustLayer) {
    basicAdjustLayer->get()->updateParamet(basicAdjustValue);
  }
  if (sharpenLayer) {
    SharpenParamet sp = {sharpenParamet.offset, sharpenParamet.sharpness};
    sharpenLayer->get()->updateParamet(sp);
  }
}

bool VkVideoRender::vaildAndInitGraph() {
  // 如果bResetFlag为true,则需要重新初始化
  if (graph && !bResetFlag) {
    onParametUpdate();
    return true;
  }
  // 重建窗口开始: getOutputLayer/getInputLayer 对外部返回空, 直至重建完成
  bRebuilding.store(true);
  VkContext* ctx = nullptr;
  graph = std::make_unique<VkPipeGraph>(ctx);
  inputLayer = graph->addNode<VkInputLayer>();
  yuv2RGBA = graph->addNode<VkYUV2RGBALayer>();
  yuv2RGBA->get()->setColorSpace(colorSpace);
  outputLayer = graph->addNode<VkOutputLayer>();
  resizeLayer = graph->addNode<VkResizeLayer>();
  if (bEnableBlend && blendImage) {
    ImageFormat bformat = blendImage->getImageFormat();
    if (bformat.width > 0 && bformat.height) {
      // 加载水印图片
      inputBlendLayer = graph->addNode<VkInputLayer>();
      inputBlendLayer->get()->inputCpuData(blendImage.get(), true);
      blendLayer = graph->addNode<VkBlendLayer>();
      blendLayer->get()->updateParamet(blendParamet);
    }
  }
  if (lutIndex > 0) {
    lutImage = std::make_unique<ImageBuffer>();
    std::string lutName =
        lutIndex == 1 ? "lookup_amatorka.bmp" : "lookup_miss_etikate.bmp";
    loadImageAsset(lutName.c_str(), lutImage.get());
    lutLayer = graph->addNode<VkLookupLayer>();
    lutLayer->get()->loadLookUp(lutImage->getPointer(),
                                lutImage->getBufferSize());
  }
  if (bBasicAdjust) {
    basicAdjustLayer = graph->addNode<VkBasicAdjustLayer>();
    basicAdjustLayer->get()->updateParamet(basicAdjustValue);
  }
  if (bSharpen) {
    sharpenLayer = graph->addNode<VkSharpenLayer>();
    SharpenParamet sp = {sharpenParamet.offset, sharpenParamet.sharpness};
    sharpenLayer->get()->updateParamet(sp);
  }
  if (bEnableAnime4K) {
    anime4KLayer = graph->addNode<VkAnime4KLayer>();
    anime4KLayer->get()->updateParamet(anime4KParamet);
  }
  if (bEnableQualityEnhance) {
    qualityEnhanceLayer = graph->addNode<VkQEnhanceLayer>();
    qualityEnhanceLayer->get()->updateParamet(qualityEnhanceParamet);
  }
  if (bEnableFSR) {
    fsrLayer = graph->addNode<VkFSRLayer>();
    fsrLayer->get()->updateParamet(fsrParamet);
  }
#ifdef AVOX_ENABLE_FREETYPE
  if (fontRender->enabled()) {
    fontLayer = graph->addNode<VkFontLayer>();
    fontRender->setFontLayer(fontLayer->get());
  }
#endif
  if (geometryRender->enabled()) {
    geometryLayer = graph->addNode<VkGeometryLayer>();
    geometryLayer->get()->setSource(geometryRender.get());
    geometryRender->setLayer(geometryLayer->get());
  }
  OutputParamet outputParamet = {};
  outputParamet.bCpu = false;
  outputParamet.bGpu = true;
  outputLayer->get()->updateParamet(outputParamet);
  outputLayer->get()->setAspect(aspect);
  // 输出
  // outputLayer->get()->addObserver(this);
  std::shared_ptr<VkPipeNode> outNode = nullptr;
  // CPU输入，则需要先转换为RGBA格式，否则直接输出
  // RGBA直接输入(bRgbaInput)时跳过yuv2RGBA层
  if (cpuIn && !bRgbaInput) {
    outNode = inputLayer->addLine(yuv2RGBA);
  } else {
    outNode = inputLayer;
  }
  if (bUseNewSize) {
    resizeParamet.bLinear = 1;
    // 优化使用scale
    if (sizeScale != 1.0f) {
      resizeParamet.newHeight = imageFormat.height * sizeScale;
      resizeParamet.newWidth = imageFormat.width * sizeScale;
    } else {
      resizeParamet.newWidth = userWidth;
      resizeParamet.newHeight = userHeight;
    }
    LOGFLF(LogLevel::info, "resize video:", imageFormat.width, "x",
           imageFormat.height, " -> ", resizeParamet.newWidth, "x",
           resizeParamet.newHeight);
    resizeLayer->get()->updateParamet(resizeParamet);
    outNode = outNode->addLine(resizeLayer);
  }
  if (bEnableAnime4K) {
    LOGFLF(LogLevel::info,
           "enable Anime4K mode:", (int32_t)anime4KParamet.mode);
    outNode = outNode->addLine(anime4KLayer);
  }
  if (bEnableQualityEnhance) {
    LOGFLF(LogLevel::info,
           "enable QualityEnhance mode:", (int32_t)qualityEnhanceParamet.outputMode);
    outNode = outNode->addLine(qualityEnhanceLayer);
  }
  if (bEnableFSR) {
    LOGFLF(LogLevel::info,
           "enable FSR scale:", (int32_t)fsrParamet.scale,
           " deblock:", fsrParamet.deblockStrength,
           " rcas:", fsrParamet.enableRCAS);
    outNode = outNode->addLine(fsrLayer);
  }
  if (bEnableBlend) {
    outNode = outNode->addLine(blendLayer);
    inputBlendLayer->addLine(blendLayer, 0, 1);
  }
  if (lutIndex > 0) {
    outNode = outNode->addLine(lutLayer);
  }
  if (basicAdjustLayer) {
    outNode = outNode->addLine(basicAdjustLayer);
  }
  if (bSharpen) {
    outNode = outNode->addLine(sharpenLayer);
  }
#ifdef AVOX_ENABLE_FREETYPE
  if (fontRender->enabled()) {
    outNode = outNode->addLine(fontLayer);
  }
#endif
  if (geometryRender->enabled()) {
    outNode = outNode->addLine(geometryLayer);
  }
  if (bOutCpuYuv) {
    rgba2YUV = graph->addNode<VkRGBA2YUVLayer>();
    // 输出设定的YUV格式
    rgba2YUV->get()->updateParamet(outYuvType);
    // 颜色矩阵(在 onInitLayer 前置入 cs, 使其用本帧 colorSpace 构矩阵)
    rgba2YUV->get()->setColorSpace(colorSpace);
    yuvOutLayer = graph->addNode<VkOutputLayer>();
    yuvOutLayer->get()->setObserver(this);
    // 设定CPU输出
    yuvOutLayer->get()->updateParamet({true, false});
    rgba2YUV->addLine(yuvOutLayer);
    outNode->addLine(rgba2YUV);
  }
  // enableImage: 每帧把处理后的 RGBA 经 GPU 缩放到 imageOutFormat 后零拷贝写入用户 buf
  // 不设 observer (区别于 YUV 的 onCpuData), 用 setOutputBuffer 直投; resetGraph 重建会重新 pin
  if (bEnableImage && imageOutBuffer) {
    imageResizeLayer = graph->addNode<VkResizeLayer>();
    ReSizeParamet imgResize = {};
    imgResize.bLinear = 1;
    imgResize.newWidth = imageOutFormat.width;
    imgResize.newHeight = imageOutFormat.height;
    imageResizeLayer->get()->updateParamet(imgResize);
    imageOutLayer = graph->addNode<VkOutputLayer>();
    imageOutLayer->get()->setOutputBuffer(imageOutBuffer);
    imageOutLayer->get()->updateParamet({true, false});
    imageResizeLayer->addLine(imageOutLayer);
    outNode->addLine(imageResizeLayer);
  }
  // 如果有surface,或者不输出CPU YUV !surface
  if (surface || !bOutCpuYuv) {
    outNode->addLine(outputLayer);
  }
  bRebuilding.store(false);
  bResetFlag = false;
  return true;
}

void VkVideoRender::releaseGraph() {
  // 图已销毁, 后续外部 getOutputLayer/getInputLayer 返回空直至下次重建
  bRebuilding.store(true);
#ifdef AVOX_ENABLE_FREETYPE
  fontRender->setFontLayer(nullptr);
#endif
  geometryRender->setLayer(nullptr);
  if (graph) {
    // 必须先等 GPU 命令完成,否则 clear() 销毁 layer 时的 descriptor pool
    // 会被 in-flight 的 command buffer 引用,触发 Vulkan 校验层错误
    graph->waitIdle();
    graph->clear();
    graph.reset();
  }
}

void VkVideoRender::onOptionChange(const char* key, ArgType option) {}

void VkVideoRender::renderGpuFrame(IRenderContext* context) {
  if (!graph) {
    log(LogLevel::warn, "renderGpuFrame failed, graph is null");
    return;
  }
  if (!context) {
    log(LogLevel::warn, "renderGpuFrame failed, context is null");
    return;
  }
  // HighClock clock = {};
  // log(LogLevel::info, "vk render cost 1:", clock.recordLast());
  inputLayer->get()->inputGpuData(context);
  // log(LogLevel::info, "vk render cost 2:", clock.recordLast());
  graph->run();
  // log(LogLevel::info, "vk render cost 3:", clock.recordLast());
}

void VkVideoRender::renderCpuFrame(const YUVFrame& frame) {
  if (yuv2RGBA->get()->getParamet() != frame.format.type) {
    yuv2RGBA->get()->updateParamet(frame.format.type);
  }
  inputLayer->get()->inputCpuData(frame, false);
  graph->run();
}

void VkVideoRender::renderCpuFrame(IImageBuffer* buffer) {
  if (!buffer) {
    return;
  }
  // 直接输入IImageBuffer到VkInputLayer(rgba8/bgra8等格式)
  // VkInputLayer会自动处理bgra8/argb8/rgb8 -> rgba8转换
  inputLayer->get()->inputCpuData(buffer, true);
  graph->run();
}

bool VkVideoRender::fetchFrame(ImageBuffer* imageBuffer) {
  if (!outputLayer) {
    return false;
  }
  return outputLayer->get()->fetchData(imageBuffer);
}

void VkVideoRender::onImageChange(const ImageFormat& format) {
  LOGFLF(LogLevel::info, "onImageChange width:", format.width,
         " height:", format.height,
         " format:", getImageTypeStr(format.imageType));
}

void VkVideoRender::onCpuData(IImageBuffer* buffer) {
  // uint8_t *data = buffer->getPointer();
  // LOGFLF(LogLevel::info, "onCpuData");
  nvBuffer = buffer;
}

void VkVideoRender::renderWindow(Window* window) {
  if (!window) {
    return;
  }
  IRenderContext* context = window->getRenderContext();
  if (context->getRenderType() == RenderType::Vulkan) {
    VkRenderContext* vkContext = static_cast<VkRenderContext*>(context);
    VkImage vkImage = vkContext->getTexture();
    VkCommandBuffer cmd = vkContext->getCommandBuffer();

    // 未初始化或重置中，只做布局转换，不渲染内容
    if (!graph || !outputLayer || !graph->resourceReady()) {
      // 将图像从 UNDEFINED 转换到 PRESENT_SRC_KHR，避免验证错误
      changeLayout(cmd, vkImage, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      return;
    }

    // 我们要把cs生成的图复制到正在渲染的图上,先改变渲染图的layout
    changeLayout(cmd, vkImage, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    outputLayer->get()->outputGpuData(vkContext);
    // 复制完成后,改变渲染图的layout准备呈现
    changeLayout(cmd, vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  } else {
    // 非Vulkan渲染
    if (!graph || !outputLayer || !graph->resourceReady()) {
      return;
    }
    outputLayer->get()->outputGpuData(context);
  }
}

void* VkVideoRender::getOutGpuBuffer() {
  if (outputLayer) {
    return outputLayer->get()->getOutGpuBuffer();
  }
  return nullptr;
}

bool VkVideoRender::getCpuFrame(YUVFrame& frame) {
  // 如果没有输出
  if (!bOutCpuYuv) {
    return false;
  }
  // 这个调用请注意与renderFrame在同一线程，否则需要把输出数据复制到nvBuffer
  if (!nvBuffer) {
    return false;
  }
  if (cpuIn) {
    frame = yuvFrame;
  } else {
    frame.pts = gpuFrame.pts;
    frame.dts = gpuFrame.dts;
    frame.keyFrame = gpuFrame.keyFrame;
  }
  // nvBuffer保持packed不原地改(同帧可能被onRenderOut和pushFrame各取一次),
  // 需要420P/422P重排时拷进splitBuffer,多次调用幂等
  if (!splitBuffer) {
    splitBuffer = std::make_unique<ImageBuffer>();
  }
  return image2SplitYUVFrame(nvBuffer, outYuvType, frame, splitBuffer.get());
}

bool VkVideoRender::getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType,
                                      int64_t* pts) {
  if (!bOutCpuYuv || !nvBuffer) {
    return false;
  }
  // packed帧直接透传,不做split重排
  *buffer = nvBuffer;
  yuvType = outYuvType;
  if (pts) {
    *pts = cpuIn ? yuvFrame.pts : gpuFrame.pts;
  }
  return true;
}

bool VkVideoRender::getGpuFrame(GpuFrame& frame) {
  if (cpuIn) {
    frame.pts = yuvFrame.pts;
    frame.dts = yuvFrame.dts;
    frame.keyFrame = yuvFrame.keyFrame;
  } else {
    frame = gpuFrame;
  }
  // 把vulkan处理GPU映射到平台的GPU资源
  // 如android是EGLImageKHR,ios是CVImageBufferRef
  // windows别的线程(如窗口)上的DX上下文,直接从outputGpuFrame取结果
#if __ANDROID__
  frame.buffer = getOutGpuBuffer();
#endif
#if __APPLE__
  frame.context = (IRenderContext*)getOutGpuBuffer();
#endif
  return true;
}
bool VkVideoRender::outputGpuFrame(IRenderContext* ctx) {
  // 这个调用请注意与renderFrame在同一线程
  if (!outputLayer) {
    return false;
  }
  outputLayer->get()->outputGpuData(ctx);
  return true;
}

}
