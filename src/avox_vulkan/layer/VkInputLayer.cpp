#include "VkInputLayer.hpp"

#include "VkPipeGraph.hpp"
#include "../share/VkSharedRender.hpp"
#ifdef __ANDROID__
#include "avox_egl/GLESContext.hpp"
#endif
#ifdef __APPLE__
#include "avox_apple/MetalContext.hpp"
#endif

namespace avox {

VkInputLayer::VkInputLayer(/* args */) {
  bInput = true;
  setUBOSize(12);
}

VkInputLayer::~VkInputLayer() {}

void VkInputLayer::onInitGraph() {
  bCpuInput = false;
  bGpuInput = false;
#ifdef WIN32
  winImage = std::make_unique<VkWinImage>();
  winImage->setVkContext(vkPipeGraph);
  bWinInterop = wphyDevcie->bInterpDx11();
#endif
#ifdef __ANDROID__
  bAndInterop = wphyDevcie->bInterpAndroid() && supportSharedGpuBuffer();
  if (bAndInterop) {
    vkAndImage = std::make_unique<VkAndImage>();
    vkAndImage->setVkContext(vkPipeGraph);
  }
#endif
#ifdef __APPLE__
  bIosInterop = true;
  vkIosImage = std::make_unique<VkIosImage>();
  vkIosImage->setVkContext(vkPipeGraph);
#endif
  // VkDevice-VkDevice 交互
  sharedImage = std::make_unique<VkSharedImage>();
  sharedImage->setVkContext(vkPipeGraph);
  if (layout->pipelineLayout != VK_NULL_HANDLE) {
    return;
  }
  std::vector<UBOLayoutItem> items = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT}};
  layout->addSetLayout(items);
  layout->generateLayout();
}

void VkInputLayer::onInitVkBuffer() {
  ImageFormat imageFormat = inFormats[0];
  ImageType imageType = imageFormat.imageType;
  bUsePipe = imageType == ImageType::rgb8 || imageType == ImageType::bgra8 ||
             imageType == ImageType::argb8;
  sizeY = 1;
  LOGFLF(LogLevel::info, "width:", imageFormat.width,
         " height:", imageFormat.height,
         " image type:", getImageTypeStr(imageFormat.imageType));
  int32_t size = getImageSize(inFormats[0]);
  if (bUsePipe) {
    std::string path = "";
    if (imageType == ImageType::rgb8) {
      path = "glsl/inputRGB.comp.spv";
    } else if (imageType == ImageType::bgra8) {
      path = "glsl/inputBRGA.comp.spv";
    } else if (imageType == ImageType::argb8) {
      path = "glsl/inputARGB.comp.spv";
    }
    shader->loadShaderModule(path);
    assert(shader->shaderStage.module != VK_NULL_HANDLE);
    int32_t imageSize = inFormats[0].width * inFormats[0].height;
    // 如果是rgb-rgba,则先buffer转cs buffer,然后cs shader转rgba.
    // 不直接在cs shader用buf->tex,兼容性考虑cpu map/cs read权限.
    if (imageType == ImageType::rgb8) {
      // 每个线程组处理240个数据,一个线程拿buffer三个数据生成四个点
      sizeX = divUp(imageSize / 4, 240);
      inBufferX = std::make_unique<VkWrapBuffer>();
      inBufferX->setVkContext(vkPipeGraph);
      inBufferX->initResoure(BufferUsage::program, imageSize * 3,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    } else if (imageType == ImageType::argb8 || imageType == ImageType::bgra8) {
      sizeX = divUp(imageSize, 240);
      inBufferX = std::make_unique<VkWrapBuffer>();
      inBufferX->setVkContext(vkPipeGraph);
      inBufferX->initResoure(BufferUsage::program, imageSize * 4,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    size = inBufferX->getBufferSize();
  }
  assert(size > 0);
  inBuffer = std::make_unique<VkWrapBuffer>();
  inBuffer->setVkContext(vkPipeGraph);
  inBuffer->initResoure(BufferUsage::store, size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        cpuBuffer->getPointer());
#ifdef WIN32
  winImage->bindD3D(vkPipeGraph->getD3D11Device(), inFormats[0]);
#endif
#ifdef __ANDROID__
  LOGFLF(LogLevel::info, "bAndInterop:", bAndInterop, " textureid:", textureId);
  if (bAndInterop && textureId > 0) {
    vkAndImage->createAndroidBuffer(outFormats[0]);
    vkAndImage->bindGL(textureId);
    LOGFLF(LogLevel::info,
           "create android shardbuffer success,textureId:", textureId,
           " width:", outFormats[0].width, " height:", outFormats[0].height);
  }
#endif
#ifdef __APPLE__
  vkIosImage->bindVK();
#endif
  LOGFLF(LogLevel::info, "end");
}

void VkInputLayer::onInitPipe() {
  if (bUsePipe) {
    outTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    layout->updateSetLayout(0, 0, &inBufferX->descInfo, &outTexs[0]->descInfo);
    auto computePipelineInfo =
        createComputePipelineInfo(layout->pipelineLayout, shader->shaderStage);
    vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                             &computePipelineInfo, nullptr, &computerPipeline);
  }
}

void VkInputLayer::onCommand() {
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  // VkDevice-VkDevice 交互: 从导入的 sharedImage 读取
  if (bVkInterop && sharedImage && sharedImage->isValid()) {
    VkImage importImage = sharedImage->getImage();
    // 导入图像布局转换: UNDEFINED → TRANSFER_SRC
    changeLayout(cmd, importImage, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    // 管线输出纹理布局转换
    outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
    // 复制: 导入图像 → 管线纹理
    copyImage(cmd, importImage, outTexs[0]->image, outFormats[0].width,
              outFormats[0].height);
    return;
  }
  VkImage vkImage = VK_NULL_HANDLE;
  if (bGpuInput) {
#ifdef WIN32
    vkImage = winImage->getImage();
#endif
#ifdef __ANDROID__
    if (bAndInterop) {
      vkImage = vkAndImage->getImage();
    }
#endif
#ifdef __APPLE__
    vkImage = vkIosImage->getImage();
#endif
    // 不需要CS处理
    log(LogLevel::info, "bUsePipe:", bUsePipe, " bGpuInput:", bGpuInput,
        " bCpuInput:", bCpuInput, " cmd:", cmd, " vkImage:", vkImage,
        " width:", outFormats[0].width, " height:", outFormats[0].height);
    if (vkImage) {
      changeLayout(cmd, vkImage, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
  }
  if (bUsePipe) {
    if (bGpuInput && vkImage) {
      imageToBuffer(cmd, vkImage, inBufferX->buffer, outFormats[0].width,
                    outFormats[0].height);

    } else if (bCpuInput) {
      VkBufferCopy copyRegion = {};
      copyRegion.size = inBufferX->descInfo.range;
      vkCmdCopyBuffer(cmd, inBuffer->buffer, inBufferX->buffer, 1, &copyRegion);
    }
    outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computerPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            layout->pipelineLayout, 0, 1,
                            layout->descSets[0].data(), 0, 0);
    vkCmdDispatch(cmd, sizeX, sizeY, 1);
  } else {
    outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (bGpuInput && vkImage) {
      copyImage(cmd, vkImage, outTexs[0]->image, outFormats[0].width,
                outFormats[0].height);
    } else if (bCpuInput) {
      bufferToImage(cmd, inBuffer.get(), outTexs[0].get(), inFormats[0].rowPitch);
    }
  }
  // External D3D11-Vulkan shared image: no need to restore layout
  // It will be re-initialized with proper layout transitions next frame
}

bool VkInputLayer::onFrame() {
  // 延迟释放: 在渲染线程上释放 Vulkan 资源
  if (bPendingRelease) {
    bVkInterop = false;
    bPendingRelease = false;
    sharedImage->release();
  }
// 测试
#ifdef __ANDROID__
  // if (vkAndImage) {
  //   vkAndImage->logData();
  // }
#endif
#ifdef WIN32
  winImage->signalFence();
#endif
  if (inBuffer && bDateUpdate) {
    inBuffer->upload(cpuBuffer->getPointer());
    bDateUpdate = false;
    return true;
  }
  return false;
}

void VkInputLayer::onUnInit() {
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

void VkInputLayer::inputGpuData(IRenderContext* context) {
  bGpuInput = true;
  ImageFormat imageFormat = inFormats[0];
#ifdef WIN32
  winImage->updateInputContext(context);
  // 根据DX11/DX12的输入
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    RenderType renderType = context->getRenderType();
    ImageFormat newFormat = {};
    if (renderType == RenderType::D3D11) {
      IDx11Context* contextDX11 = (IDx11Context*)context;
      ID3D11Texture2D* dxtexture = contextDX11->getTexture();
      if (!dxtexture) {
        return;
      }
      getImageFormat(dxtexture, newFormat);
    } else if (renderType == RenderType::D3D12) {
      IDx12Context* contextDX12 = (IDx12Context*)context;
      ID3D12Resource* dxtexture = contextDX12->getTexture();
      if (!dxtexture) {
        return;
      }
      getImageFormat(dxtexture, newFormat);
    }
    LOGFLF(LogLevel::info,
           "dx texture render type:", getVRenderTypeStr(renderType),
           " width:", newFormat.width, " height:", newFormat.height,
           " image type:", getImageTypeStr(newFormat.imageType));
    setLayerFormat(this, newFormat);
    // if (winImage->getRenderType() != context->getRenderType()) {
    //   winImage->setRenderType(context->getRenderType());
    //   // 需要重置重新生成DX共享交互资源
    //   resetGraph();
    //   return;
    // }
  }
  // 管线资源已经准备完成
  // if (vkPipeGraph->resourceReady()) {
  //   // 把另外线程的GPU资源复制到共享纹理中
  //   if (winImage && winImage->getInit()) {
  //     winImage->dx11CopyTemp(context);
  //   }
  // }
#endif
#ifdef __ANDROID__
  GLESContext* glesContext = static_cast<GLESContext*>(context);
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    imageFormat = glesContext->getImageFormat();
    if (imageFormat.imageType == ImageType::other) {
      imageFormat.imageType = ImageType::rgba8;
    }
    textureId = glesContext->getImage();
    LOGFLF(LogLevel::info, "gles context textureId:", textureId,
           " width:", imageFormat.width, " height:", imageFormat.height,
           " image type:", getImageTypeStr(imageFormat.imageType));
    setLayerFormat(this, imageFormat);
  }
#endif
#ifdef __APPLE__
  MetalContext* metalContext = static_cast<MetalContext*>(context);
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    imageFormat = metalContext->getImageFormat();
    IOSurfaceRef ioSurface = metalContext->getIOSurface();
    vkIosImage->setIOSurface(ioSurface);
    log(LogLevel::info,
        "VkInputLayer::inputGpuData metal context ioSurface:", ioSurface,
        " width:", imageFormat.width, " height:", imageFormat.height,
        " image type:", getImageTypeStr(imageFormat.imageType));
    setLayerFormat(this, imageFormat);
  }
#endif
}

void VkInputLayer::onFormatChange() {
  ImageFormat imageFormat = cpuBuffer->getImageFormat();
  setLayerFormat(this, imageFormat);
  int32_t size = getImageSize(inFormats[0]);
  LOGFLF(LogLevel::info, "width:", imageFormat.width,
         " height:", imageFormat.height,
         " image type:", getImageTypeStr(imageFormat.imageType),
         " size:", size);
}

}
