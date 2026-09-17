#include "VkVideoRender.hpp"

#include <cstring>

#include <algorithm>

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
  // 注: 不做"参数相同跳过重建"的幂等 — VkInputLayer 的 CPU 数据仅在
  // inputCpuData(建图时)搬运, 跳过重建会让刷新的水印像素到不了 GPU。
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
  if (!bEnableBlend) {
    return;
  }
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
  if (c.standard == colorSpace.standard && c.range == colorSpace.range &&
      c.transfer == colorSpace.transfer) {
    return;
  }
  if (c.transfer != colorSpace.transfer) {
    LOGFLF(LogLevel::info, "colorspace transfer:", (int32_t)c.transfer,
           " (0=gamma 1=linear 2=pq 3=hlg)");
  }
  colorSpace = c;
  if (rgba2YUV) {
    rgba2YUV->get()->setColorSpace(c);
  }
  if (yuv2RGBA) {
    yuv2RGBA->get()->setColorSpace(c);
  }
}

// HDR 静态元数据: 峰值亮度变化才重传
void VkVideoRender::setHdrMeta(const HdrMeta& meta) {
  if (hdrMeta.valid == meta.valid && hdrMeta.maxCLL == meta.maxCLL &&
      hdrMeta.maxLuminance == meta.maxLuminance) {
    return;
  }
  LOGFLF(LogLevel::info, "hdr meta maxLum:", meta.maxLuminance,
         " minLum:", meta.minLuminance, " cll:", meta.maxCLL,
         " fall:", meta.maxFALL);
  hdrMeta = meta;
  if (yuv2RGBA) {
    yuv2RGBA->get()->setHdrMeta(meta);
  }
}

// HDR 输出模式: forceHDR 跳过 tone map, 仅变化时下发
void VkVideoRender::setHdrMode(HdrMode mode) {
  if (mode == hdrMode) {
    return;
  }
  LOGFLF(LogLevel::info, "set hdrMode:", (int32_t)mode);
  hdrMode = mode;
  if (yuv2RGBA) {
    yuv2RGBA->get()->setHdrMode(mode);
  }
}

#ifdef AVOX_ENABLE_FREETYPE
FontRender* VkVideoRender::enableRenderFont() {
  if (fontRender->enabled()) {
    return fontRender.get();  // 已开启: 图上已有该层, 无需求重建
  }
  fontRender->setEnable(true);
  bResetFlag = true;
  return fontRender.get();
}

void VkVideoRender::disableRenderFont() {
  if (!fontRender->enabled()) {
    return;
  }
  fontRender->setEnable(false);
  fontRender->setFontLayer(nullptr);
  bResetFlag = true;
}
#endif

GeometryRender* VkVideoRender::enableRenderGeometry() {
  if (geometryRender->enabled()) {
    return geometryRender.get();  // 已开启: 图上已有该层, 无需求重建
  }
  geometryRender->setEnable(true);
  bResetFlag = true;
  return geometryRender.get();
}

void VkVideoRender::disableRenderGeometry() {
  if (!geometryRender->enabled()) {
    return;
  }
  // 对象常驻，仅让 VkGeometryLayer 在下次 graph 重建时不接入执行链
  geometryRender->setEnable(false);
  geometryRender->setLayer(nullptr);
  bResetFlag = true;
}

ICanvasLayer* VkVideoRender::enableRenderCanvas() {
  if (bEnableCanvas) {
    return canvasRender.get();  // 已开启: 图上已有该层, 无需求重建
  }
  bEnableCanvas = true;
  bResetFlag = true;
  // CanvasRender 为稳定前端(层随图重建换指针), 外部长期持有
  return canvasRender.get();
}

void VkVideoRender::disableRenderCanvas() {
  if (!bEnableCanvas) {
    return;
  }
  bEnableCanvas = false;
  canvasRender->setCanvasLayer(nullptr);
  bResetFlag = true;
}

void VkVideoRender::enableSizeChange(int32_t width, int32_t height) {
  // sizeScale==1 时图走 userWidth/userHeight 分支, 两者相同即无变化
  if (bUseNewSize && sizeScale == 1.0f && userWidth == width &&
      userHeight == height) {
    return;
  }
  bUseNewSize = true;
  sizeScale = 1.0f;
  userWidth = width;
  userHeight = height;
  bResetFlag = true;
}

void VkVideoRender::enableSizeScale(float scale) {
  if (bUseNewSize && sizeScale != 1.0f && sizeScale == scale) {
    return;
  }
  bUseNewSize = true;
  sizeScale = scale;
  bResetFlag = true;
}

void VkVideoRender::disableSizeChange() {
  if (!bUseNewSize) {
    return;
  }
  bUseNewSize = false;
  bResetFlag = true;
}

void VkVideoRender::enableAnime4K(const Anime4KParamet& paramet) {
  // 参数相同且已开启: 跳过重建(结构体 POD, memcmp 比对; 不等则照常重建)
  if (bEnableAnime4K &&
      0 == std::memcmp(&paramet, &anime4KParamet, sizeof(Anime4KParamet))) {
    return;
  }
  anime4KParamet = paramet;
  bEnableAnime4K = true;
  // 互斥: 开启Anime4K时关闭QualityEnhance和FSR
  bEnableQualityEnhance = false;
  bEnableFSR = false;
  bResetFlag = true;
}

void VkVideoRender::disableAnime4K() {
  if (!bEnableAnime4K) {
    return;
  }
  bEnableAnime4K = false;
  bResetFlag = true;
}

void VkVideoRender::enableQualityEnhance(const QualityEnhanceParamet& paramet) {
  if (bEnableQualityEnhance && 0 == std::memcmp(&paramet, &qualityEnhanceParamet,
                                                sizeof(QualityEnhanceParamet))) {
    return;
  }
  qualityEnhanceParamet = paramet;
  bEnableQualityEnhance = true;
  // 互斥: 开启画质增强时关闭Anime4K和FSR
  bEnableAnime4K = false;
  bEnableFSR = false;
  bResetFlag = true;
}

void VkVideoRender::disableQualityEnhance() {
  if (!bEnableQualityEnhance) {
    return;
  }
  bEnableQualityEnhance = false;
  bResetFlag = true;
}

void VkVideoRender::enableFSR(const FSRParamet& paramet) {
  if (bEnableFSR &&
      0 == std::memcmp(&paramet, &fsrParamet, sizeof(FSRParamet))) {
    return;
  }
  fsrParamet = paramet;
  bEnableFSR = true;
  // 互斥: 开启FSR时关闭Anime4K和QualityEnhance
  bEnableAnime4K = false;
  bEnableQualityEnhance = false;
  bResetFlag = true;
}

void VkVideoRender::disableFSR() {
  if (!bEnableFSR) {
    return;
  }
  bEnableFSR = false;
  bResetFlag = true;
}

// 视角钳位(SDK内持视角真相): 180°族yaw/pitch各±90°(1°边界余量,出界即无内容),
// 360°族yaw取模包绕; fov钳[30,120](防超源分辨率极限/畸变不可看)
static void clampVrView(const VrParamet& p, VrViewState& s) {
  bool bFull = p.projection == VrProjection::fisheye360 ||
               p.projection == VrProjection::equirect360;
  if (bFull) {
    if (s.yaw < -180.0f) {
      s.yaw += 360.0f;
    }
    if (s.yaw >= 180.0f) {
      s.yaw -= 360.0f;
    }
  } else {
    s.yaw = std::min(std::max(s.yaw, -89.0f), 89.0f);
  }
  s.pitch = std::min(std::max(s.pitch, -89.0f), 89.0f);
  s.fov = std::min(std::max(s.fov, 30.0f), 120.0f);
}

// VrParamet(全帧uv) + 源尺寸 -> VrLayerGeom(eye局部uv);
// 圆心/半径全零用默认(内切每眼画幅高、居中), 半径默认0.5×眼高
static VrLayerGeom buildVrGeom(const VrParamet& p, int32_t srcW, int32_t srcH,
                               int32_t outW, int32_t outH) {
  VrLayerGeom g = {};
  g.projection = (int32_t)p.projection;
  g.fisheyeFov = p.fisheyeFov;
  bool bSbs = p.eyeLayout == VrEyeLayout::sbs;
  float eyePxW = bSbs ? srcW * 0.5f : (float)srcW;
  float eyePxH = bSbs ? (float)srcH : srcH * 0.5f;
  g.eyeW = bSbs ? 0.5f : 1.0f;
  g.eyeH = bSbs ? 1.0f : 0.5f;
  g.eyeLu = 0.0f;
  g.eyeLv = 0.0f;
  g.eyeRu = bSbs ? 0.5f : 0.0f;
  g.eyeRv = bSbs ? 0.0f : 0.5f;
  float defLu = g.eyeLu + g.eyeW * 0.5f;
  float defLv = g.eyeLv + g.eyeH * 0.5f;
  float defRu = g.eyeRu + g.eyeW * 0.5f;
  float defRv = g.eyeRv + g.eyeH * 0.5f;
  bool bHasL = p.centerL[0] != 0.0f || p.centerL[1] != 0.0f;
  bool bHasR = p.centerR[0] != 0.0f || p.centerR[1] != 0.0f;
  float lu = bHasL ? p.centerL[0] : defLu;
  float lv = bHasL ? p.centerL[1] : defLv;
  float ru = bHasR ? p.centerR[0] : defRu;
  float rv = bHasR ? p.centerR[1] : defRv;
  g.cLu = (lu - g.eyeLu) / g.eyeW;
  g.cLv = (lv - g.eyeLv) / g.eyeH;
  g.cRu = (ru - g.eyeRu) / g.eyeW;
  g.cRv = (rv - g.eyeRv) / g.eyeH;
  float rLpx = p.radiusL > 0.0f ? p.radiusL * (float)srcH : 0.5f * eyePxH;
  float rRpx = p.radiusR > 0.0f ? p.radiusR * (float)srcH : 0.5f * eyePxH;
  g.rLu = rLpx / eyePxW;
  g.rLv = rLpx / eyePxH;
  g.rRu = rRpx / eyePxW;
  g.rRv = rRpx / eyePxH;
  g.outWidth = outW;
  g.outHeight = outH;
  return g;
}

// VR输出尺寸: 有宿主设置(enableSizeChange/Scale)按其来,
// 否则给显示级默认(VR输出是视口图, 不能按8K源尺寸走)
static void resolveVrOutSize(bool bUseNewSize, float sizeScale, int32_t userW,
                             int32_t userH, int32_t srcW, int32_t srcH,
                             int32_t& outW, int32_t& outH) {
  if (bUseNewSize) {
    if (sizeScale != 1.0f) {
      outW = (int32_t)(srcW * sizeScale);
      outH = (int32_t)(srcH * sizeScale);
    } else {
      outW = userW;
      outH = userH;
    }
    return;
  }
#if __ANDROID__
  outW = 1280;
  outH = 720;
#else
  outW = 1920;
  outH = 1080;
#endif
}

void VkVideoRender::enableVr(const VrParamet& paramet) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  if (bEnableVr && vrParamet == paramet) {
    return;
  }
  vrParamet = paramet;
  bEnableVr = true;
  bResetFlag = true;
}

void VkVideoRender::disableVr() {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  if (!bEnableVr) {
    return;
  }
  bEnableVr = false;
  bResetFlag = true;
}

void VkVideoRender::rotateView(float deltaYaw, float deltaPitch) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  vrViewPending.yaw += deltaYaw;
  vrViewPending.pitch += deltaPitch;
  clampVrView(vrParamet, vrViewPending);
  bVrViewDirty = true;
}

void VkVideoRender::zoomView(float deltaFov) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  vrViewPending.fov += deltaFov;
  clampVrView(vrParamet, vrViewPending);
  bVrViewDirty = true;
}

void VkVideoRender::resetView() {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  vrViewPending = {};
  bVrViewDirty = true;
}

void VkVideoRender::getViewAngles(float* yaw, float* pitch, float* fov) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  if (yaw) {
    *yaw = vrViewPending.yaw;
  }
  if (pitch) {
    *pitch = vrViewPending.pitch;
  }
  if (fov) {
    *fov = vrViewPending.fov;
  }
}

void VkVideoRender::setVrOutMode(VrOutMode mode) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  vrViewPending.outMode = (int32_t)mode;
  bVrViewDirty = true;
}

void VkVideoRender::setVrStereoStrength(float strengthDeg) {
  std::lock_guard<std::mutex> lock(vrViewMutex);
  vrViewPending.stereo = std::min(std::max(strengthDeg, 0.0f), 5.0f);
  bVrViewDirty = true;
}

vec2i VkVideoRender::getOutSize() {
  vec2i size = {imageFormat.width, imageFormat.height};
  if (bEnableVr) {
    size = {vrGeom.outWidth, vrGeom.outHeight};
  } else if (bUseNewSize) {
    size = {resizeParamet.newWidth, resizeParamet.newHeight};
  }
  return size;
}

void VkVideoRender::onParametUpdate() {
  // VR视角快速通道: 渲染线程消费宿主线程的 pending(rotateView 入队侧)
  if (vrLayer) {
    std::lock_guard<std::mutex> lock(vrViewMutex);
    if (bVrViewDirty) {
      vrView = vrViewPending;
      bVrViewDirty = false;
    }
    vrLayer->get()->setViewState(vrView);
  }
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
  // 先消费再重建: 重建耗时数十毫秒, 期间宿主线程新置的请求留到下一帧,
  // 不会被这次重建的收尾清掉(末尾清零会吞请求, 表现为"开关不生效")
  bResetFlag = false;
  // 重建窗口开始: getOutputLayer/getInputLayer 对外部返回空, 直至重建完成
  bRebuilding.store(true);
  VkContext* ctx = nullptr;
  graph = std::make_unique<VkPipeGraph>(ctx);
  inputLayer = graph->addNode<VkInputLayer>();
  yuv2RGBA = graph->addNode<VkYUV2RGBALayer>();
  yuv2RGBA->get()->setColorSpace(colorSpace);
  if (hdrMeta.valid) {
    yuv2RGBA->get()->setHdrMeta(hdrMeta);
  }
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
  if (bEnableVr) {
    vrLayer = graph->addNode<VkVrLayer>();
    int32_t vrOutW = 0;
    int32_t vrOutH = 0;
    {
      std::lock_guard<std::mutex> lock(vrViewMutex);
      resolveVrOutSize(bUseNewSize, sizeScale, userWidth, userHeight,
                       imageFormat.width, imageFormat.height, vrOutW, vrOutH);
      vrGeom = buildVrGeom(vrParamet, imageFormat.width, imageFormat.height,
                           vrOutW, vrOutH);
      if (bVrViewDirty) {
        vrView = vrViewPending;
        bVrViewDirty = false;
      }
      vrLayer->get()->updateParamet(vrGeom);
      vrLayer->get()->setViewState(vrView);
    }
    LOGFLF(LogLevel::info, "enable VR proj:", (int32_t)vrParamet.projection,
           " layout:", (int32_t)vrParamet.eyeLayout, " out:", vrOutW, "x",
           vrOutH);
  }
#ifdef AVOX_ENABLE_FREETYPE
  if (fontRender->enabled()) {
    fontLayer = graph->addNode<VkFontLayer>();
    fontRender->setFontLayer(fontLayer->get());
  }
#endif
  if (bEnableCanvas) {
    canvasLayer = graph->addNode<VkCanvasLayer>();
    canvasRender->setCanvasLayer(canvasLayer->get());
  }
  if (geometryRender->enabled()) {
    geometryLayer = graph->addNode<VkGeometryLayer>();
    geometryLayer->get()->setSource(geometryRender.get());
    geometryRender->setLayer(geometryLayer->get());
  }
  OutputParamet outputParamet = {};
  outputParamet.bCpu = false;
  // bGpu = 有真实呈现/互操作消费者; 离屏时 display 支路仅截图 sink,
  // 关掉避免每帧 fence 与各平台 interop 资源分配
  outputParamet.bGpu = surface != nullptr;
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
  if (bEnableVr) {
    // VR投影自带视口尺寸, 替代 resize; 后续画质层作用于透视后的视口图
    // (= 输出端增强, 上采样发软由 FSR/Anime4K 在视口分辨率补偿)
    outNode = outNode->addLine(vrLayer);
  } else if (bUseNewSize) {
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
  // 字幕画布层在字体(OSD/SRT)之后、几何层之前: ASS/PGS 与 SRT 互斥,
  // 顺序晚于所有画质层, 保证字幕不被超分/增强重采样(计划 §3.4 挂载位)
  if (canvasLayer) {
    outNode = outNode->addLine(canvasLayer);
  }
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
  // display 支路始终连图: 无 surface 时它只作 fetchFrame 截图 sink
  // (bGpu 随 surface 关, 不产生 fence/interop 副作用); 曾按
  // surface||!bOutCpuYuv 裁剪, 离屏+cpuYuv(录制/yuvout/截图)时未连边,
  // fetchFrame 读到空纹理 → vk 车道截图恒败(shot-vk 已知取舍的根因)
  outNode->addLine(outputLayer);
  bRebuilding.store(false);
  return true;
}

void VkVideoRender::releaseGraph() {
  // 图已销毁, 后续外部 getOutputLayer/getInputLayer 返回空直至下次重建
  bRebuilding.store(true);
#ifdef AVOX_ENABLE_FREETYPE
  fontRender->setFontLayer(nullptr);
#endif
  geometryRender->setLayer(nullptr);
  // 层随图销毁, 前端脱钩(来件暂存, 下次建图后补发)
  canvasRender->setCanvasLayer(nullptr);
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
