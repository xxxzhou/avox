#pragma once

#include "avox/layer/VInputLayer.hpp"
#ifdef WIN32
#include "../windows/VkWinImage.hpp"
#elif __ANDROID__
#include "../android/VkAndImage.hpp"
#elif __APPLE__
#include "../ios/VkIosImage.hpp"
#endif
#include "../share/VkSharedImage.hpp"
#include "VkLayer.hpp"

namespace avox {

// 把各种VideoFormat转化成ImageFormat,主要二种,R8/RGBA8
// VkInputLayer不支持CPU/GPU同时输入,否则会忽视CPU输入
class VkInputLayer : public VInputLayer, public VkLayer {
  AVOX_LAYER_GETNAME(VkInputLayer)

 private:
  std::unique_ptr<VkWrapBuffer> inBuffer = nullptr;
  // 如果需要GPU计算,需要先把inBuffer copy 到 gpu local
  std::unique_ptr<VkWrapBuffer> inBufferX = nullptr;
  // 是否需要GPU计算
  bool bUsePipe = false;
  // 如windows设定只支持RGBA
  bool bGPUFormat = false;
#ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<VkAndImage> vkAndImage = nullptr;
  uint32_t textureId = 0;
  // 上次bindGL时GLES侧的EGLContext; GLES重建后纹理id常被复用, 须连它一起判换新
  void* bindEglCtx = nullptr;
  bool bAndInterop = false;
#elif __APPLE__
  std::unique_ptr<VkIosImage> vkIosImage = nullptr;
  bool bIosInterop = false;
#endif
  // VkDevice-VkDevice 交互
  std::unique_ptr<VkSharedImage> sharedImage = nullptr;
  bool bVkInterop = false;
  bool bPendingRelease = false;
  // 10bit 平面(r16)输入改喂 RGBA8 字节视图(每 texel=2 个 u16 字, 高 0.75×帧高);
  // r16ui 存储图在部分移动驱动上 imageLoad 恒零(Adreno 实测特性位齐全仍读零)
  bool bByteView = false;
  // 打包数据真实字节数: 奇数字行数时缓冲尾半行是填充, 上传只搬真实数据
  int32_t cpuDataBytes = 0;

 public:
  VkInputLayer(/* args */);
  virtual ~VkInputLayer();

  // VkDevice-VkDevice 交互访问
 public:
  VkSharedImage* getSharedImage() const { return sharedImage.get(); }
  void setVkInterop(bool bEnable) {
    if (bVkInterop != bEnable) {
      bVkInterop = bEnable;
      // 标志变化需重录 command buffer,使 interop 拷贝路径生效/失效
      resetGraph();
    }
  }
  void requestRelease() { bPendingRelease = true; }

  // InputLayer
 public:
  virtual void inputGpuData(IRenderContext* context) override;

  // VInputLayer
 protected:
  virtual void onFormatChange() override;

  // VkLayer
 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitVkBuffer() override;
  virtual void onInitPipe() override;
  virtual void onCommand() override;
  virtual bool onFrame() override;

  virtual void onUnInit() override;
};

}