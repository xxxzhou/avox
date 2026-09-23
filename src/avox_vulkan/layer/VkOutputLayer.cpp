#include "VkOutputLayer.hpp"

#ifdef WIN32
#include <windows.h>
#endif

#include "VkPipeGraph.hpp"
#include "../share/VkSharedRender.hpp"
#ifdef __ANDROID__
#include "avox_egl/GLESContext.hpp"
#endif

#include <atomic>

namespace avox {

extern vec4i getTextureRect(int texWidth, int texHeight, float aspect);

// 输出图世代全局计数: 层实例重造也不回退, 消费端跨帧比较用
static std::atomic<uint64_t> s_outputGeneration{0};

VkOutputLayer::VkOutputLayer(/* args */) { bOutput = true; }

VkOutputLayer::~VkOutputLayer() {}

void VkOutputLayer::onInitGraph() {
#ifdef WIN32
  winImage = std::make_unique<VkWinImage>();
  winImage->setVkContext(vkPipeGraph);
  bWinInterop = wphyDevcie->bInterpDx11();
  if (bDx11Output) {
    // 底层自建共享纹理模式: 每帧管线拷入, 外部 DX11 设备经 NT 句柄读取
    winImage->setInteropType(InteropType::output);
  }
#elif __ANDROID_API__ >= 26
  bAndInterop = wphyDevcie->bInterpAndroid() && supportSharedGpuBuffer();
  if (bAndInterop) {
    vkAndImage = std::make_unique<VkAndImage>();
    vkAndImage->setVkContext(vkPipeGraph);
  }
  log(LogLevel::info, "bAndInterop:", bAndInterop);
#elif __APPLE__
  vkIosImage = std::make_unique<VkIosImage>();
  vkIosImage->setVkContext(vkPipeGraph);
  log(LogLevel::info, "bIosInterop:", bIosInterop);
#endif
  // VkDevice-VkDevice 交互
  sharedImage = std::make_unique<VkSharedImage>();
  sharedImage->setVkContext(vkPipeGraph);
}

void VkOutputLayer::onUpdateParamet() {
  if (paramet.bGpu != oldParamet.bGpu) {
    resetGraph();
  }
}

void VkOutputLayer::onInitVkBuffer() {
  // 导出资源(bindD3D/createIOSurface/createAndroidBuffer/sharedImage)本轮重造,
  // 世代先递增, 本帧起 SurfaceRenderEvent 携带新值
  generation = ++s_outputGeneration;
  // 考虑rowPatch
  patchFormat = inFormats[0];
  int32_t pixelSize = getPixelSize(inFormats[0].imageType);
  int32_t rowPitch = inFormats[0].width * pixelSize;
  patchFormat.rowPitch = rowPitch;
  if (rowPitch % kMinAlign != 0) {
    rowPitch = (rowPitch / kMinAlign + 1) * kMinAlign;
    patchFormat.rowPitch = rowPitch;
    LOGFLF(LogLevel::info, "rowPitch:", rowPitch, " width:", inFormats[0].width,
           " imageType:", getImageTypeStr(inFormats[0].imageType));
  }
  int32_t size = getImageSize(patchFormat);
  assert(size > 0);
  // CPU输出
  outBuffer = std::make_unique<VkWrapBuffer>();
  outBuffer->setVkContext(vkPipeGraph);
  outBuffer->initResoure(BufferUsage::store, size,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  // cpuData.resize(size);
#if defined(__ANDROID__)
  // Android: outFormat 由外部 GL 纹理尺寸驱动(见 outputGpuData), 只在未设置时
  // 用上游尺寸兜底(建立期 createAndroidBuffer 不能拿到 0)
  if (outFormat.width == 0 || outFormat.height == 0) {
    outFormat = inFormats[0];
  }
#else
  // 输出尺寸每次重建都跟随上游: 原先只在 0x0 时取一次, 换源改分辨率后
  // viewRect / Apple IOSurface 重建会一直沿用旧尺寸
  if (outFormat.width != inFormats[0].width ||
      outFormat.height != inFormats[0].height ||
      outFormat.imageType != inFormats[0].imageType) {
    LOGFLF(LogLevel::info, "outFormat sync, from:", outFormat.width, "x",
           outFormat.height, " to:", inFormats[0].width, "x",
           inFormats[0].height);
  }
  outFormat = inFormats[0];
#endif
#ifdef WIN32
  if (paramet.bGpu && bWinInterop) {
    LOGFLF(LogLevel::info, "[dx11dbg] onInitVkBuffer call bindD3D, dx11Output:",
           bDx11Output, " outW:", outFormats[0].width, " outH:", outFormats[0].height);
    winImage->bindD3D(vkPipeGraph->getD3D11Device(), outFormats[0]);
    LOGFLF(LogLevel::info, "[dx11dbg] onInitVkBuffer bindD3D returned, init:",
           winImage->getInit() ? 1 : 0);
  } else {
    LOGFLF(LogLevel::info, "[dx11dbg] onInitVkBuffer skip bindD3D, bGpu:",
           (int32_t)paramet.bGpu, " bWinInterop:", bWinInterop,
           " bDx11Output:", bDx11Output);
  }
#endif
#if __ANDROID_API__ >= 26
  LOGFLF(LogLevel::info, "bAndInterop:", bAndInterop,
         " gpu out:", paramet.bGpu);
  if (bAndInterop && paramet.bGpu) {
    vkAndImage->createAndroidBuffer(outFormat);
  }
#endif
#if __APPLE__
  vkIosImage->createIOSurface(outFormat, rowPitch);
#endif
  dispatch(&IVOutputLayerOb::onImageChange, inFormats[0]);
}

bool VkOutputLayer::onFrame() {
  // 延迟释放: 在渲染线程上释放 Vulkan 资源
  if (bPendingRelease) {
    bVkInterop = false;
    bPendingRelease = false;
    sharedImage->release();
  }
  if (paramet.bCpu && inFormats.size() > 0) {
    // 在管线vkQueueSubmit之后调用，否则可能CPU读到旧数据
    outBuffer->flush(true);
    cpuBuffer->setData(outBuffer->getCpuData(), patchFormat, false);
    dispatch(&IVOutputLayerOb::onCpuData, cpuBuffer.get());
    // enableImage: 零拷贝把 staging 映射内存引用进用户 buffer (非拷贝)
    if (userOutBuffer) {
      userOutBuffer->copyFrom(cpuBuffer.get(), false);
    }
  }
  if (paramet.bGpu) {
#ifdef WIN32
    winImage->signalFence();
    dispatch(&IVOutputLayerOb::onGpuProcess);
#endif
#ifdef __ANDROID__
//  if(vkAndImage) {
//      vkAndImage->logData();
//  }
#endif
#ifdef __APPLE__
//      if(vkIosImage){
//          vkIosImage->logData();
//      }
#endif
  }
  return true;
}

void VkOutputLayer::onUnInit() {
  VkLayer::onUnInit();
#ifdef WIN32
  winImage.reset();
#endif
#ifdef __ANDROID__
  vkAndImage.reset();
#endif
#ifdef __APPLE__
  vkIosImage.reset();
#endif
  sharedImage.reset();
}

void VkOutputLayer::onCommand() {
#ifdef WIN32
  // 直通帧闸: 消费端把上一帧 blit 完才放行本帧写入共享纹理,
  // 消除 avox 写(N+1) 与消费端读(N) 跨 API 并发的块级拼贴。
  // 100ms 防呆: 消费端卡死/关闭时降级为旧行为, 不拖死管线。
  if (frameGateEv && bDx11Output && winImage && winImage->getInit()) {
    ::WaitForSingleObject(frameGateEv, 100);
  }
#endif
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  if (paramet.bCpu) {
    inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT);
    imageToBuffer(cmd, inTexs[0].get(), outBuffer.get(), patchFormat.rowPitch);
  }
  // 暂时限定这二种格式可以映射到别的平台GPU资源
  bool bCanMapGpu = outFormat.imageType == ImageType::rgba8 ||
                    outFormat.imageType == ImageType::bgra8;
  if (paramet.bGpu && bCanMapGpu) {
    VkImage destImage = VK_NULL_HANDLE;
    bool bInterop = false;
    // 映射到别的平台GPU资源
    vec4i viewRect = {};
    viewRect.x = 0;
    viewRect.y = 0;
    viewRect.z = outFormat.width;
    viewRect.w = outFormat.height;
#ifdef WIN32
    bInterop = bWinInterop && winImage->getInit();
    destImage = winImage->getImage();
#endif
#if __ANDROID_API__ >= 26
    if (bAndInterop) {
      destImage = vkAndImage->getImage();
      bInterop = true;
    }
#endif
#ifdef __APPLE__
    destImage = vkIosImage->getImage();
    bInterop = true;
#endif
    if (bInterop && destImage) {
      // 字幕等计算层输出停在 GENERAL 布局, copyImage 支持源 GENERAL, 免转移
      if (inTexs[0]->layout != VK_IMAGE_LAYOUT_GENERAL) {
        inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT);
      }
      changeLayout(cmd, destImage, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
#ifdef WIN32
      // 源(上游节点纹理)与本层输出纹理尺寸不等时 1:1 copyImage 只覆盖左上角重叠区
      // (不报错也不缩放), 换分辨率后表现为左上角一块 + 其余黑; 尺寸不等改走带 rect
      // 的 blit 缩放铺满, 并打日志, 不再静默截断。尺寸一致仍走拷贝(等价且更快)
      const ImageFormat& dstFormat = winImage->getFormat();
      bool bSizeMismatch =
          dstFormat.width > 0 && dstFormat.height > 0 &&
          (inTexs[0]->width != (uint32_t)dstFormat.width ||
           inTexs[0]->height != (uint32_t)dstFormat.height);
      if (bSizeMismatch) {
        LOGFLF(LogLevel::warn, "dx11 output size mismatch, src:",
               (int32_t)inTexs[0]->width, "x", (int32_t)inTexs[0]->height,
               " dst:", dstFormat.width, "x", dstFormat.height, " blit to fit");
        vec4i dstRect = {0, 0, dstFormat.width, dstFormat.height};
        blitFillImage(cmd, inTexs[0].get(), winImage->getImage(), dstRect);
      } else {
        // 因为dx窗口是直接使用CS的纹理,viewport
        copyImage(cmd, inTexs[0].get(), winImage->getImage());
      }
      changeLayout(cmd, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
#endif
#if __ANDROID_API__ >= 26
      blitFillImage(cmd, inTexs[0].get(), vkAndImage->getImage(), viewRect.z,
                    viewRect.w);

#endif
#ifdef __APPLE__
      // blitFillImage会做RGBA到BRGA的转化
      blitFillImage(cmd, inTexs[0].get(), vkIosImage->getImage(), viewRect.z,
                    viewRect.w);
#endif
    }
  }
  // VkDevice-VkDevice 交互: 复制到导出的 sharedImage
  if (bVkInterop && sharedImage && sharedImage->isValid()) {
    VkImage exportImage = sharedImage->getImage();
    if (inTexs[0]->layout != VK_IMAGE_LAYOUT_GENERAL) {
      inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT);
    }
    changeLayout(cmd, exportImage, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    // 导出面尺寸由调用方声明(enableVkOutput 的 w/h), 与图内出图尺寸不等时
    // 1:1 copyImage 会静默截断到左上角, 改走 blit 缩放铺满导出面
    const VkSharedImageDesc& exportDesc = sharedImage->getDesc();
    bool bSizeMismatch =
        exportDesc.width > 0 && exportDesc.height > 0 &&
        (inTexs[0]->width != (uint32_t)exportDesc.width ||
         inTexs[0]->height != (uint32_t)exportDesc.height);
    if (bSizeMismatch) {
      LOGFLF(LogLevel::warn, "vkshare output size mismatch, src:",
             (int32_t)inTexs[0]->width, "x", (int32_t)inTexs[0]->height,
             " dst:", exportDesc.width, "x", exportDesc.height, " blit to fit");
      vec4i dstRect = {0, 0, exportDesc.width, exportDesc.height};
      blitFillImage(cmd, inTexs[0].get(), exportImage, dstRect);
    } else {
      copyImage(cmd, inTexs[0].get(), exportImage);
    }
    // 拷入后留在 GENERAL: 跨 VkDevice 共享的标准约定, 外部设备拷出/采样
    // 不经过 UNDEFINED 丢弃语义 (UE 直通读黑问题, 2026-09-05)
    changeLayout(cmd, exportImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  }
}

void VkOutputLayer::outputGpuData(IRenderContext* context) {
  if (!context || !vkPipeGraph->resourceReady()) {
    return;
  }
  if (context->getRenderType() == RenderType::Vulkan) {
    IVkRenderContext* vkRender = (IVkRenderContext*)context;
    // 同 VkDevice 窗口显示渲染结果,拿到窗口的commandbuffer
    if (vkRender->getCommandBuffer()) {
      if (inTexs.empty() || !inTexs[0] || !inTexs[0]->image) {
        return;
      }
      // GPU输出
      VkCommandBuffer copyCmd = vkRender->getCommandBuffer();
      // 把计算结果拷贝到渲染纹理上
      VkImage copyImage = vkRender->getTexture();
      ImageFormat format = vkRender->getImageFormat();
      vec4i viewRect = {};
      viewRect.x = 0;
      viewRect.y = 0;
      viewRect.z = format.width;
      viewRect.w = format.height;
      // 自适应长宽
      if (aspect != 0.0f && format.height != 0) {
        viewRect = getTextureRect(format.width, format.height, aspect);
      }
      if (viewRect.z == 0 || viewRect.w == 0) {
        LOGFLF(LogLevel::warn, "vkcontext no vaild,width:", viewRect.z,
               " height:", viewRect.w);
        return;
      }
      // 如果aspect变化后,先清空原copyImage
      if (baspectChange) {
        clearCount = 5;
        baspectChange = false;
        LOGFLF(LogLevel::info,
               "clear vkwindow backgroud,aspect change:", aspect);
      }
      if (clearCount) {
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        vkCmdClearColorImage(copyCmd, copyImage,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor,
                             1, &range);
        clearCount--;
      }
      // blitFillImage 内部会处理布局转换
      blitFillImage(copyCmd, inTexs[0].get(), copyImage, viewRect);
    }
  }
#ifdef WIN32
  if (context->getRenderType() == RenderType::D3D11 ||
      context->getRenderType() == RenderType::D3D12) {
    if (!winImage) {
      return;
    }
    winImage->updateOutputContext(context);
    if (winImage->getRenderType() != context->getRenderType()) {
      winImage->setRenderType(context->getRenderType());
      // 需要重置重新生成DX共享交互资源
      resetGraph();
      return;
    }
  }
#elif __ANDROID__
  if (context->getRenderType() == RenderType::OpenGLES) {
    GLESContext* glesContext = (GLESContext*)context;
    int bindType = GL_TEXTURE_2D;
    if (paramet.bCpu && !paramet.bGpu) {
      if (outBuffer && outBuffer->getCpuData()) {
        glBindTexture(bindType, 0);
        // glActiveTexture(GL_TEXTURE0);
        glBindTexture(bindType, glesContext->getImage());
        glTexSubImage2D(bindType, 0, 0, 0, inFormats[0].width,
                        inFormats[0].height, GL_RGBA, GL_UNSIGNED_BYTE,
                        outBuffer->getCpuData());
        glBindTexture(bindType, 0);
      }
      // log(LogLevel::info,"android cpu copy");
    }
    // android api < 26不能使用GPU数据转换,只能用CPU数据传输
    if (bAndInterop && paramet.bGpu) {
      ImageFormat format = vkAndImage->getFormat();
      ImageFormat glesFormat = glesContext->getImageFormat();
      if (glesFormat.width == 0 || glesFormat.height == 0) {
        glesFormat.width = inFormats[0].width;
        glesFormat.height = inFormats[0].height;
      }
      // gles 传入的纹理大小改变
      if (format.width != glesFormat.width ||
          format.height != glesFormat.height) {
        format.width = glesFormat.width;
        format.height = glesFormat.height;
        format.imageType = ImageType::rgba8;
        // opengles相关资源resetGraph使用outFormat
        outFormat = format;
        // 重新生成opengles资源与cmdbuffer
        resetGraph();
        return;
      }
      // EGLImage与opengles映射
      vkAndImage->bindGL(glesContext->getImage(), bindType);
      // log(LogLevel::info,"android gpu copy");
    }
  }
#endif
}

bool VkOutputLayer::fetchData(IImageBuffer* buffer) {
  // 截帧路径不走run()恢复门, 必须自判: 恢复窗/重建未完成时纹理随旧device
  // 已消亡, 独立提交只会喂无效句柄给loader(UAF/堆损坏)
  if (!vkPipeGraph || !vkPipeGraph->resourceReady()) {
    return false;
  }
  if (inTexs.size() <= 0 || !inTexs[0]) {
    return false;
  }
  // 只拿取一帧,单独建立一个VkCommandBuffer

  vkCommand = std::make_unique<VkCommand>();
  vkCommand->setVkContext(vkPipeGraph);
  VkCommandBuffer cmd = vkCommand->getCommandBuffer();
  inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT);
  imageToBuffer(cmd, inTexs[0].get(), outBuffer.get());
  inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT);
  // 提交失败/等待超时: outBuffer内容不可信, 严禁再download到调用方buffer
  if (!vkCommand->submit()) {
    return false;
  }
  // 下载到CPU
  if (outBuffer && outBuffer->getBufferSize() > 0) {
    // patchFormat
    buffer->setImageFormat(patchFormat);
    outBuffer->download(buffer->getPointer());
    // AvoxData data = {buffer->getPointer() + 1233192, 45, true};
    // log(LogLevel::info, "fetchData buffer ptr:", data);
    return true;
  }
  return false;
}

void* VkOutputLayer::getOutGpuBuffer() {
#ifdef __ANDROID__
  if (vkAndImage) {
    return vkAndImage.get();
  }
#endif
#ifdef __APPLE__
  if (vkIosImage) {
    return (IRenderContext*)vkIosImage.get();
  }
#endif
#ifdef WIN32
  // 返回NT共享句柄
  if (winImage) {
    return winImage->getHandle();
  }
#endif
  return nullptr;
}

void VkOutputLayer::setAspect(float aspect_) {
  if (aspect != aspect_) {
    LOGFLF(LogLevel::info, "setAspect:", aspect_);
    baspectChange = true;
    winCmd = nullptr;
  }
  aspect = aspect_;
}

#ifdef WIN32
void VkOutputLayer::setDx11Output(bool bDx11) {
  if (bDx11Output == bDx11) {
    return;
  }
  bDx11Output = bDx11;
  if (winImage) {
    if (bDx11Output) {
      winImage->setInteropType(InteropType::output);
    }
    // 已建图后开关需重绑共享纹理, 重置管线走 onInitVkBuffer 重新 bindD3D
    resetGraph();
  }
}
#endif

}
