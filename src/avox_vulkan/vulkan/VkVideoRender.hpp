#pragma once

#include "../quality/VkAnime4KLayer.hpp"
#include "../extra/VkColorAdjustmentLayer.hpp"
#include "../extra/VkImageProcessing.hpp"
#include "../extra/VkLookupLayer.hpp"
#include "../quality/VkFSRLayer.hpp"
#include "../quality/VkQEnhanceLayer.hpp"
#include "../layer/VkBlendLayer.hpp"
#include "../layer/VkInputLayer.hpp"
#include "../layer/VkOutputLayer.hpp"
#include "../layer/VkPipeGraph.hpp"
#include "../layer/VkRGBA2YUVLayer.hpp"
#include "../layer/VkResizeLayer.hpp"
#include "../layer/VkYUV2RGBALayer.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/video/VideoRender.hpp"

#include <atomic>

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FontRender.hpp"
#endif
#include "../geometry/GeometryRender.hpp"

namespace avox {

class VkVideoRender : public VideoRender, public IVOutputLayerOb {
 public:
  VkVideoRender();
  virtual ~VkVideoRender();

 protected:
  std::unique_ptr<VkPipeGraph> graph = nullptr;
  VKTNodePtr<VkInputLayer> inputLayer = nullptr;
  VKTNodePtr<VkYUV2RGBALayer> yuv2RGBA = nullptr;
  VKTNodePtr<VkOutputLayer> outputLayer = nullptr;
  bool bPause = false;
  VBufferType bufferType = VBufferType::cpu;
  // 是否添加水印
  bool bEnableBlend = false;
  BlendParamet blendParamet = {};
  std::unique_ptr<ImageBuffer> blendImage = nullptr;
  VKTNodePtr<VkInputLayer> inputBlendLayer = nullptr;
  VKTNodePtr<VkBlendLayer> blendLayer = nullptr;
  // 是否用新大小
  bool bUseNewSize = false;
  float sizeScale = 1.0f;
  int32_t userWidth = 1280;
  int32_t userHeight = 720;
  ReSizeParamet resizeParamet = {};
  VKTNodePtr<VkResizeLayer> resizeLayer = nullptr;
  // Anime4K超分
  bool bEnableAnime4K = false;
  Anime4KParamet anime4KParamet = {};
  VKTNodePtr<VkAnime4KLayer> anime4KLayer = nullptr;
  // 画质增强 (Real-ESRGAN, 与 Anime4K/FSR 互斥)
  bool bEnableQualityEnhance = false;
  QualityEnhanceParamet qualityEnhanceParamet = {};
  VKTNodePtr<VkQEnhanceLayer> qualityEnhanceLayer = nullptr;
  // 实时增强 (FSR+双边, 与 Anime4K/QualityEnhance 互斥)
  bool bEnableFSR = false;
  FSRParamet fsrParamet = {};
  VKTNodePtr<VkFSRLayer> fsrLayer = nullptr;
  // 是否启用LUT
  int32_t lutIndex = 0;
  VKTNodePtr<VkLookupLayer> lutLayer = nullptr;
  std::unique_ptr<ImageBuffer> lutImage = nullptr;
  // 图重建窗口标志: 重建期间外部经 getOutputLayer/getInputLayer 拿到空,
  // 避免 enable/disable 类调用与半重建状态竞态 (渲染线程置位)
  std::atomic<bool> bRebuilding{false};
  // 基础图像调整(色调/亮度/对比度/饱和度/伽玛) - 单组开关
  bool bBasicAdjust = false;
  BasicAdjustParamet basicAdjustValue = {};
  VKTNodePtr<VkBasicAdjustLayer> basicAdjustLayer = nullptr;
  // 锐度
  bool bSharpen = false;
  SharpenVideo sharpenParamet = {};
  VKTNodePtr<VkSharpenLayer> sharpenLayer = nullptr;
  // 软编一般要求输出为YUV420P格式,硬编是NV12格式
  // IOutFrame里的bOutCpu用来控制是否输出YUV420P格式的CPU资源
  VKTNodePtr<VkRGBA2YUVLayer> rgba2YUV = nullptr;
  VKTNodePtr<VkOutputLayer> yuvOutLayer = nullptr;
  // enableImage: 独立缩放+输出分支 (区别于 YUV 的 rgba2YUV/yuvOutLayer, 不走 onCpuData)
  VKTNodePtr<VkResizeLayer> imageResizeLayer = nullptr;
  VKTNodePtr<VkOutputLayer> imageOutLayer = nullptr;
  IImageBuffer* nvBuffer = nullptr;
  // 录制/播放颜色空间, 驱动 rgba2YUV/yuv2RGBA 的转换矩阵(与 encoder tag 同源)
  ColorSpaceDesc colorSpace{YuvStandard::bt601, YuvRange::full};

#ifdef AVOX_ENABLE_FREETYPE
  std::unique_ptr<FontRender> fontRender = nullptr;
  VKTNodePtr<VkFontLayer> fontLayer = nullptr;
#endif
  // 几何叠加层（线/矩形/点/圆），无外部依赖，随 Vulkan 一并启用
  std::unique_ptr<GeometryRender> geometryRender = nullptr;
  VKTNodePtr<VkGeometryLayer> geometryLayer = nullptr;

 public:
  // 图重建期间(渲染线程)返回空: 外部 enableVkOutput 等拿不到层, 自然失败下帧重试,
  // 避免与半重建状态竞态。bRebuilding 于 vaildAndInitGraph/releaseGraph 置位
  VkOutputLayer* getOutputLayer() {
    if (!outputLayer || bRebuilding.load()) {
      return nullptr;
    }
    return outputLayer->get();
  }
  VkInputLayer* getInputLayer() {
    if (!inputLayer || bRebuilding.load()) {
      return nullptr;
    }
    return inputLayer->get();
  }

  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  // 参数更新 (管线不需要重建时)
  void onParametUpdate();
  // virtual void renderGpuFrame(const GpuFrame &frame) override;
  virtual void renderCpuFrame(const YUVFrame& frame) override;
  // 静态图像CPU输入(rgba8/bgra8等),跳过yuv2RGBA层
  virtual void renderCpuFrame(IImageBuffer* buffer) override;
  virtual bool fetchFrame(ImageBuffer* imageBuffer) override;

 public:
  // 水印
  void enableWatermark(const Watermark& paramet, IImageBuffer* imageBuffer);
  void disableWatermark();
  // Lut
  void enableLut(const LutParamet& paramet);
  void disableLut();
  // 基础图像调整: enableBasicAdjust 传入整组参数, disableBasicAdjust 关整组
  void enableBasicAdjust(const BasicAdjustParamet& paramet);
  void disableBasicAdjust();
  // 锐度
  void updateSharpen(const SharpenVideo& paramet);
  void disableSharpen();
  // 颜色空间(矩阵), 运行时重传, 不重建 graph
  void setColorSpace(const ColorSpaceDesc& c);
#ifdef AVOX_ENABLE_FREETYPE
  // 获取字体层
  FontRender* enableRenderFont();
  void disableRenderFont();
#endif
  // 几何叠加层
  GeometryRender* enableRenderGeometry();
  void disableRenderGeometry();
  void enableSizeChange(int32_t width, int32_t height);
  void enableSizeScale(float scale);
  void disableSizeChange();
  // Anime4K
  void enableAnime4K(const Anime4KParamet& paramet);
  void disableAnime4K();
  // 画质增强 (与 Anime4K/FSR 互斥)
  void enableQualityEnhance(const QualityEnhanceParamet& paramet);
  void disableQualityEnhance();
  // 实时增强 (FSR+双边, 与 Anime4K/QualityEnhance 互斥)
  void enableFSR(const FSRParamet& paramet);
  void disableFSR();
  // 应用大小变化后,返回变化后的大小
  vec2i getOutSize();

 public:
  virtual IRenderContext* getGpuContext() override;
  virtual void renderGpuFrame(IRenderContext* context) override;
  virtual void renderWindow(Window* window) override;
  virtual void* getOutGpuBuffer() override;

  // IVOutputLayerOb
 public:
  virtual void onImageChange(const ImageFormat& format) override;
  virtual void onCpuData(IImageBuffer* buffer) override;

  // IOption
 public:
  virtual void onOptionChange(const char* key, ArgType option) override;

  // IOutFrame
 public:
  // 返回CPU资源
  virtual bool getCpuFrame(YUVFrame& frame) override;
  virtual bool getGpuFrame(GpuFrame& frame) override;
  // Vk类型的GPU资源转换到Dx11/OpenGL/Metal类型的GPU资源
  virtual bool outputGpuFrame(IRenderContext* ctx) override;
};

}
