# IOS硬解经IOSurface高效到Vulkan管线

在Andoird有[Android硬解经AHardwareBuffer高效到Vulkan管线](https://zhuanlan.zhihu.com/p/1933240978434679422)，windows有[播放器FFmpeg](https://zhuanlan.zhihu.com/p/1924537408311001536)/[Vulkan与DX11交互](https://zhuanlan.zhihu.com/p/349534525)，都能把硬解出来的原生GPU纹理映射到Vulkan纹理。现在就差一个Apple平台了，和Android一样，原来硬解出来后GPU数据要给Vulkan管线，通过CVPixelBufferLockBaseAddress把数据mpa到内存，比较低效，和windows/android平台一样，搜索相应的跨线程/进程图像共享方案，windows是纹理共享句柄，android是AHardwareBuffer，而IOS就是IOSurface，相比前二者文档还有一些，而IOSurface本身资料较少，其Metal/IOSurface/Vulkan交互的资料就更少了，花了一天时间，串联各个AI提供的资料与代码，总算把这个流程跑起来了。

## 输出到IOSurface

和Android类似，原来硬解的NV12纹理经opengl处理后输出到RGBA纹理上，再使用AHardwareBuffer对接Vulkan，当前也是类似，先把硬解的NV12纹理使用Metal转成RGBA格式的GPU数据，也是二个考虑，一是兼容原来的Metal窗口，二是使用RGBA格式在Metal/IOSurface/Vulkan交互会是最简单的。

在[播放器 IOS](https://zhuanlan.zhihu.com/p/1924537772141679194)的MetalGraph原输出到窗口后逻辑，添加输出到IOSurface的逻辑。

修改原MetalGraph里的initSurface逻辑，在窗口为空时，创建RGBA的IOSurface。

``` C++
void MetalGraph::initSurface(CAMetalLayer *metalLayer_) {
  metalLayer = metalLayer_;
  createPipelineState();
  createTextureCache();
  // 新增：当metalLayer为空时创建输出纹理
  if (!metalLayer) {
      // 创建 IOSurface 属性字典
      NSDictionary *surfaceProps = @{
          (id)kIOSurfaceWidth: @(imageFormat.width)，
          (id)kIOSurfaceHeight: @(imageFormat.height)，
          (id)kIOSurfacePixelFormat: @(kCVPixelFormatType_32RGBA)，
          (id)kIOSurfaceBytesPerElement: @(4)，
          (id)kIOSurfaceBytesPerRow: @(imageFormat.width * 4)
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
      LOGFLF(LogLevel::info，"ioSurface:"，ioSurface，" output texture:"，outputTexture);
    }
}
// 根据窗口是否为空，修改渲染目标
void MetalGraph::updateNV12ToMetalLayer(CVImageBufferRef imageBuffer) {
  id<CAMetalDrawable> drawable = nil;
  id<MTLTexture> targetTexture = nil;
  // 修改：根据metalLayer存在情况选择渲染目标
  if (metalLayer) {
    drawable = [metalLayer nextDrawable];
    targetTexture = drawable.texture;
  } else {
    targetTexture = outputTexture;
  }
  ...
}
```

## IOSurface绑定Vulkan

好像没什么好说的，和[Android硬解经AHardwareBuffer高效到Vulkan管线](https://zhuanlan.zhihu.com/p/1933240978434679422)里的HardwareImage(现改为VkAndImage)类似，VkIosImage把IOSurface与VkImage映射在一起。

``` C++
void VkIosImage::bindVK(IOSurfaceRef ioSurface) {
    size_t surfaceWidth = IOSurfaceGetWidth(ioSurface);
    size_t surfaceHeight = IOSurfaceGetHeight(ioSurface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(ioSurface);
    size_t pixelFormat = (size_t)IOSurfaceGetPixelFormat(ioSurface);
    LOGFLF(LogLevel::info，"surface width:"，surfaceWidth，" surface height:"，surfaceHeight，" bytesPerRow:"，bytesPerRow，" pixelFormat:"，pixelFormat);
    if(surfaceWidth <=0 || surfaceHeight <=0){    
        return;
    }
    // 将 IOSurfaceRef 映射到 VkImage
    VkImportMetalIOSurfaceInfoEXT importInfo = {};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT;
    importInfo.ioSurface = ioSurface;
    importInfo.pNext = nullptr;
    
    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.pNext = &importInfo;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent.width = surfaceWidth;
    imageCreateInfo.extent.height = surfaceHeight;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VkResult result = vkCreateImage(vkDevice， &imageCreateInfo， nullptr， &vkImage);
    if (result != VK_SUCCESS) {
        LOGFLF(LogLevel::warn， "failed to create image");
        return;
    }
    // 分配并绑定设备内存
    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(vkDevice， vkImage， &memoryRequirements);
    uint32_t memoryTypeIndex = 0;
    bool getIndex = wphyDevcie->getMemoryTypeIndex(memoryRequirements.memoryTypeBits， VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT，
                                                   memoryTypeIndex);
    if(!getIndex){
        LOGFLF(LogLevel::warn， "failed to get memory type index");
        return;
    }
    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;
    vkAllocateMemory(vkDevice， &memoryAllocateInfo， nullptr， &memory);
    
    // 绑定
    vkBindImageMemory(vkDevice， vkImage， memory， 0);
}
```

## 组合流程

和Android硬解里的改动几乎一样，还好前面刚改过，直接照着来。

``` C++
class VkVideoRender : public VideoRender，
                      public RunTask，
                      public IVOutputLayerOb {
 VCodecTh codecTH = VCodecTh::other;
#ifdef WIN32
  IRenderContext *dx11Context = nullptr;
#endif
#ifdef __ANDROID__
  std::unique_ptr<GLPipeGraph> glPipeGraph = nullptr;
#endif
#ifdef __APPLE__
  std::unique_ptr<MetalGraph> metalGraph = nullptr;
#endif
}
void VkVideoRender::start() {
    ...
if (videoDecoder) {
    codecTH = videoDecoder->getCodecTh();
    LOGFLF(LogLevel::info， "codecTh:"， (int32_t)codecTH);
#ifdef __APPLE__
    if (codecTH == VCodecTh::iosVT) {
      metalGraph = std::make_unique<MetalGraph>();
      const DecoderParams& params = videoDecoder->getDecoderParams();
      ImageFormat imageFormat = {};
      imageFormat.width = params.width;
      imageFormat.height = params.height;
      imageFormat.imageType = ImageType::rgba8;
      metalGraph->updateImageFormat(imageFormat);      
    }
#endif  
}
    ...
  if (codecTH == VCodecTh::other || codecTH == VCodecTh::cpu) {
    inputLayer->addLine(yuv2RGBA)->addLine(outputLayer);
  } else {
    inputLayer->addLine(outputLayer);
  }
  ...    
}

void VkVideoRender::onRunTask() {
#ifdef __ANDROID__  
  if (codecTH == VCodecTh::androidMC) {
    glPipeGraph->initGraph();    
    log(LogLevel::info， "android harder decoder，user vk render");
  }
#endif
#ifdef __APPLE__
  if (codecTH == VCodecTh::iosVT) {
    metalGraph->initSurface(nullptr);
    log(LogLevel::info, "ios harder decoder,user vk render");
  }
#endif
  while (running()) {
    bool bYield = false;
    // 拿到帧数据如何处理
    auto frameAction = [&](const VideoFramePtr &frame) {
        if (frame->buffer->getCodecTh() == VCodecTh::cpu) {
            ...
        }
#ifdef WIN32
      if (frame->buffer->getCodecTh() == VCodecTh::dx11) {
        ...
      }       
#endif 
#ifdef __ANDROID__
      if (frame->buffer->getCodecTh() == VCodecTh::androidMC) {
        ...
      }
#endif 
#ifdef __APPLE__
    if (frame->buffer->getCodecTh() == VCodecTh::iosVT) {
      HwVideoBuffer *hwBuffer =
            dynamic_cast<HwVideoBuffer *>(frame->buffer.get());
        if (hwBuffer && metalGraph) { 
            CVImageBufferRef imageBuffer =
              (CVImageBufferRef)hwBuffer->getHwBuffer();
            // 把NV12的CVImageBufferRef处理成RGBA的IOSurfaceRef
            metalGraph->updateNV12ToMetalLayer(imageBuffer);
            // IOSurfaceRef绑定Vkimage
            inputLayer->get()->inputGpuData(metalGraph.get());
            // 运行管线
            graph->run();
        }
        // 释放
        frame->release(true);
      }
    }
  }
#endif      
}
```

输入层VkInputLayer添加相应逻辑。

``` C++
class VkInputLayer : public VInputLayer， public VkLayer {
#ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<VkAndImage> vkAndImage = nullptr;
  uint32_t textureId = 0;
  bool bAndInterop = false;
#elif __APPLE__
  std::unique_ptr<VkIosImage> vkIosImage = nullptr;
  IOSurfaceRef ioSurface = nullptr;
  bool bIosInterop = false;
#endif    
}
void VkInputLayer::inputGpuData(IRenderContext *context) {
  bGpuInput = true;
  ImageFormat imageFormat = inFormats[0];
#ifdef __APPLE__
  bIosInterop = true;
  vkIosImage = std::make_unique<VkIosImage>();
  vkIosImage->setVkContext(vkPipeGraph);
#endif
}
void VkInputLayer::onInitVkBuffer() {
#ifdef __ANDROID__
  LOGFLF(LogLevel::info， "bAndInterop:"， bAndInterop);
  if (bAndInterop) {
    vkAndImage->createAndroidBuffer(outFormats[0]);
    vkAndImage->bindGL(textureId);
  }
#endif
#ifdef __APPLE__
  vkIosImage->bindVK(ioSurface);
#endif
}
void VkInputLayer::inputGpuData(IRenderContext *context) {
  bGpuInput = true;
  ImageFormat imageFormat = inFormats[0];
#ifdef __APPLE__
  MetalContext *metalContext = dynamic_cast<MetalContext *>(context);
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    imageFormat = metalContext->getImageFormat();   
    ioSurface = metalContext->getIOSurface(); 
    log(LogLevel::info，
        "VkInputLayer::inputGpuData metal context ioSurface:"， ioSurface，
        " width:"， imageFormat.width， " height:"， imageFormat.height，
        " image type:"， getImageTypeStr(imageFormat.imageType));
    setLayerFormat(this， imageFormat);
  }
#endif
}
```

注意，VkInputLayer/VkOutputLayer/VkVideoRender链接了带IOS平台特性的文件，如果只是Apple平台，把cpp改成mm后缀就行，我最开始使用这种方式，但是会导致其在andorid下编译不过，相反在windows平台倒是可以。后经查找资料，找到现比较简洁的方式就是在Apple平台时，CMake链接时指定这几个cpp文件用objc++编译，别的不变。

``` CMake
# vulkan可用
if(AVOX_ENABLE_VULKAN)
    add_sub_path(avox_vulkan AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/layer AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/vulkan AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/extra AVOX_HEADER AVOX_SOURCE)    
    # 根据平台设置如下几个CPP文件 的编译语言
    if(APPLE)
        set_source_files_properties(avox_vulkan/layer/VkInputLayer.cpp PROPERTIES LANGUAGE OBJCXX)
        set_source_files_properties(avox_vulkan/layer/VkOutputLayer.cpp PROPERTIES LANGUAGE OBJCXX)
        set_source_files_properties(avox_vulkan/vulkan/VkVideoRender.cpp PROPERTIES LANGUAGE OBJCXX)
    endif()
    if(WIN32)
        add_sub_path(avox_vulkan/windows AVOX_HEADER AVOX_SOURCE)
    endif()
    if(ANDROID)
        add_sub_path(avox_vulkan/android AVOX_HEADER AVOX_SOURCE)
    endif()
    if(APPLE)
        add_sub_path(avox_vulkan/ios AVOX_HEADER AVOX_SOURCE)
    endif()
    ...
endif()    
```

到此，所有平台硬解出来的数据都能直接把GPU数据映射到Vulkan计算管线了，当然，windows/android/ios分别保留了dx11|dx12/egl/Metal原生窗口呈现，但是没有图像处理管线，只是为了兼容各平台低版本原生显示，现在多平台默认现在都会走硬解，原生渲染处理YUV转RGBA GPU显存，RGBA显存映射vulkan图像管线上处理，Vulkan窗口呈现，这样所有平台终于能统一高效GPGPU处理管线了。

现在各个平台自己对接看起来有点乱，后面有时间再重构成一个类，分别在不同平台管理句柄/AHardwareBuffer/IOSurface，其分别可以映射dx11/dx12/opengl/metal/vulkan，现在这种使用方式还是不太方便，等有时间再把这几个类重构下。







