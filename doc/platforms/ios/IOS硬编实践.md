# IOS硬编实践

和前面[Android MediaCodec GPU编码实践](../android/Android硬编GPU输入.md)一样,主要记录如何在IOS使用硬编,以及把相机直出的帧,再经Vulkan处理后的GPU数据直接硬编.

## 获取相机帧

本来是想和android ndk camera2一样,专门记录下的,但是用AI对照AndCameraSource给我实现一个IOSCameraSource后,发现改改就能用了,android camera出的帧和解码出来的帧还有些区别,需要完善我之前的GLESContext,添加识别线程是否已生成EGLContext并利用,而IOS相机出的帧和解码对上层开发者来说,几乎没区别,直接利用之前写的MetalRender/VulkanRender就能渲染,代码简单修改下就能适配了,好像也没太多能讲的,直接上相机输出帧的代码.

```c++
void IOSCameraSource::handleVideoOutput(AVCaptureOutput *captureOutput,
                                        CMSampleBufferRef sampleBuffer,
                                        AVCaptureConnection *connection) {
  if (!sampleBuffer) {
    return;
  }
  // CMSampleBufferGetImageBuffer返回的CVImageBufferRef 是由 CMSampleBufferRef
  // 拥有的 系统处理完 sampleBuffer 后，会自动释放其包含的
  // imageBuffer,不用手动释放
  CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!imageBuffer) {
    return;
  }
  // 获取时间戳
  CMTime presentationTime =
      CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
  int64_t timestamp = CMTimeGetSeconds(presentationTime) * 1000;
  // 获取图像信息
  size_t width = CVPixelBufferGetWidth(imageBuffer);
  size_t height = CVPixelBufferGetHeight(imageBuffer);
  // 创建GpuFrame,与IOSVDecoder相同的数据结构
  GpuFrame frame = {};
  frame.format.width = (int)width;
  frame.format.height = (int)height;
  frame.format.type = YuvType::nv12;
  frame.pts = timestamp > 0 ? timestamp : timeStampMS();
  frame.dts = frame.pts;
  frame.buffer = (void *)imageBuffer;
  // 渲染处理或是编码处理
  onFrame(frame);
  // 当前帧由CMSampleBufferRef管理，不需要手动释放
  // CFRelease(imageBuffer);
}
```

## VideoToolbox编码

这里就不讲整个流程了,让AI对照AndVEncoder实现IOSVEncoder,整个流程基本上就可用了,主要是讲二个需要注意的位置.

一是IOS编码回调输出里没有配置帧,和android的不一样,是会在I帧前出vps/sps/pps这些配置帧的,在IOS编码里,如果想保存成视频或是推流出去,需要在I帧前手动获取配置帧并给到封装对象.

二是注意IOS编码输出的数据前四字节是表示长度的,而获取的配置帧是没有这个长度的,一般来说,最好统一加上这个长度,后面由封装器把看情况决定是否把这四字节转成annexb头,注意配置帧是有顺序的,需要让顺序写入,他们之间的解析有依赖关系.

android硬编SDK中,相机与编码都让你操作Surface,类似GPU数据队列,拿出帧数据处理后再给另外队列,并因为EGL环境与线程有对应关系,用C++来进行图像处理再给丢给另一边的Surface有些复杂,当然如果只是操作Surface这层,java层处理就能很舒服,而IOS这边,相机与编码都可直接操作CMSampleBufferRef对象,并隐藏GPU上下文的操作,对C++开发就很友好了,因此直接把IOSCameraSource里的CMSampleBufferRef拿给VideoToolbox也就很方便,没什么好说的.

前面说过,现在所有平台的图像处理都由vulkan处理,[IOS硬解经IOSurface高效到Vulkan管线](https://zhuanlan.zhihu.com/p/1933834616818636429)这里解码出来和相机出来的CMSampleBufferRef都可以直接通过IOSurface交由vulkan处理,在这主要记录下如何把vulkan处理后的数据又转到IOSurface,并传给VideoToolbox编码.

在数据处理后,vulkan image来到VkOutputLayer,添加之前VkInputLayer里metal-vulkan的GPU数据交互VkIosImage类,把处理后的数据映射给VkIosImage.

```c++
// Vk输出结果层
class VkOutputLayer : public VOutputLayer, public VkLayer {
#ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<VkAndImage> vkAndImage = nullptr;
  bool bAndInterop = false;
#elif __APPLE__
  std::unique_ptr<VkIosImage> vkIosImage = nullptr;
  bool bIosInterop = true;
#endif  
}
// 节点添加到PipeGraph中时动作
void VkOutputLayer::onInitGraph() {
#ifdef WIN32
  winImage = std::make_unique<VkWinImage>();
  winImage->setVkContext(vkPipeGraph);
  bWinInterop = wphyDevcie->bInterpDx11();
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
}
// 当PipeGraph开始初始化
void VkOutputLayer::onInitVkBuffer() {
#ifdef WIN32
  if (paramet.bGpu && bWinInterop) {
    winImage->bindD3D(vkPipeGraph->getD3D11Device(), outFormats[0]);
  }
#endif
#if __ANDROID_API__ >= 26
  if (bAndInterop && paramet.bGpu) {
    vkAndImage->createAndroidBuffer(outFormat);
  }
#endif
#if __APPLE__  
  vkIosImage->createIOSurface(outFormat, rowPitch);
#endif    
}
// 生成gpu的执行command
void VkOutputLayer::onCommand() {
  if (paramet.bGpu) {
    VkImage destImage = VK_NULL_HANDLE;
    bool bInterop = false;
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
      inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
      changeLayout(cmd, destImage, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
#ifdef WIN32
      copyImage(cmd, inTexs[0].get(), winImage->getImage());
      changeLayout(cmd, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
#endif
#if __ANDROID_API__ >= 26
      blitFillImage(cmd, inTexs[0].get(), vkAndImage->getImage(),
                    vkAndImage->getFormat().width,
                    vkAndImage->getFormat().height);

#endif
#ifdef __APPLE__
//        copyImage(cmd, inTexs[0].get(), vkIosImage->getImage());
//        changeLayout(cmd, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//                     VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
//                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        // 因为CVPixelBufferCreateWithIOSurface限制,只能输出BRGA,blitFillImage会做RGBA到BRGA的转化
        blitFillImage(cmd, inTexs[0].get(), vkIosImage->getImage(),
                      inFormats[0].width,
                      inFormats[0].height);
#endif
    }
  }
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
  return nullptr;
}
```

最新的VkIosImage,主要是添加如下代码完善vulkan->metal的数据交互.

```c++
// 需要让IRenderContext在前,不然后面转void*,再转vkiosimage指针会偏移
class VkIosImage : public IRenderContext, public VkContextRef {
public:
  // metal->vulkan 输入传入IOSurfaceRef
  void setIOSurface(IOSurfaceRef ioSurface);
  // vulkan->metal 输出IOSurfaceRef
  void createIOSurface(const ImageFormat& imageFormat,int32_t rowPitch); 
  void bindVK(); 
}
void VkIosImage::createIOSurface(const ImageFormat& imageFormat,int32_t rowPitch){
   int32_t pixelSize = getPixelSize(imageFormat.imageType);
   if(rowPitch < pixelSize*imageFormat.width){
    rowPitch = pixelSize*imageFormat.width;
   }
    // 后面把ioSurface转CVPixelBuffer,但是格式只支持BGRA,不支持RGBA
    // CVPixelBufferCreateWithIOSurface
   OSType pixelType = kCVPixelFormatType_32BGRA;
    switch (imageFormat.imageType) {
        case ImageType::rgba8:
            pixelType= kCVPixelFormatType_32BGRA;
            break;
        case ImageType::bgra8:
            pixelType= kCVPixelFormatType_32BGRA;
            break;
        case ImageType::r8:
            pixelType= kCVPixelFormatType_OneComponent8;
            break;
        default:
            pixelType: kCVPixelFormatType_32BGRA;
            break;
        }
   NSDictionary *surfaceProps = @{
      (id)kIOSurfaceWidth : @(imageFormat.width),
      (id)kIOSurfaceHeight : @(imageFormat.height),
      (id)kIOSurfacePixelFormat : @(pixelType),
      (id)kIOSurfaceBytesPerElement : @(pixelSize),
      (id)kIOSurfaceBytesPerRow : @(rowPitch)
    };
  ioSurface = IOSurfaceCreate((CFDictionaryRef)surfaceProps);
  bCreate = true;
  LOGFLF(LogLevel::info,"create io surface:",ioSurface," vkiosimage:",this);
  bindVK();
}
void VkIosImage::bindVK() {  
    if(!ioSurface)  {
        LOGFLF(LogLevel::warn,"iosurface is null");
        return;
    }
    size_t surfaceWidth = IOSurfaceGetWidth(ioSurface);
    size_t surfaceHeight = IOSurfaceGetHeight(ioSurface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(ioSurface);
    OSType pixelFormat = IOSurfaceGetPixelFormat(ioSurface);
    LOGFLF(LogLevel::info,"surface width:",surfaceWidth," surface height:",surfaceHeight," bytesPerRow:",bytesPerRow," pixelFormat:",pixelFormat);
    if(surfaceWidth <=0 || surfaceHeight <=0){
        return;
    }
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    switch (pixelFormat) {
        case kCVPixelFormatType_32BGRA:
            format = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        case kCVPixelFormatType_32RGBA:
            format= VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case kCVPixelFormatType_OneComponent8:
            format= VK_FORMAT_R8_UNORM;
            break;
        default:
            format= VK_FORMAT_R8G8B8A8_UNORM;
            break;
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
    imageCreateInfo.format = format;
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
    
    VkResult result = vkCreateImage(vkDevice, &imageCreateInfo, nullptr, &vkImage);
    if (result != VK_SUCCESS) {
        LOGFLF(LogLevel::warn, "failed to create image");
        return;
    }
    // 分配并绑定设备内存
    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(vkDevice, vkImage, &memoryRequirements);
    uint32_t memoryTypeIndex = 0;
    bool getIndex = wphyDevcie->getMemoryTypeIndex(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                   memoryTypeIndex);
    if(!getIndex){
        LOGFLF(LogLevel::warn, "failed to get memory type index");
        return;
    }
    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;
    vkAllocateMemory(vkDevice, &memoryAllocateInfo, nullptr, &memory);
    // 绑定
    vkBindImageMemory(vkDevice, vkImage, memory, 0);
}
```

简单来说,创建一个IOSurfaceRef,并把这个IOSurfaceRef映射到一个VkImage,后续修改这个VkImage,就相当于修改这个IOSurfaceRef.和Android里的AHardwareBuffer类似.

注意,不知是不是机器还是系统版本限制,创建RGBA的IOSurfaceRef,后续CVPixelBufferCreateWithIOSurface拿不到,所以只能使用BGRA,这样在拿最后图像处理后的VkImage时,就不能用vkCmdCopyImage,因为管线中图像处理结果最后大概率是RGBA格式的,格式不同,就需要做转换,不能直接用vkCmdCopyImage,改用vkCmdBlitImage,会自动把RGBA转BGBA的.

和安卓类似,如果是vulkan的GPU输出,需要在原frame做个变化,后续编码检查到这个frame才能确定是metal还是vulkan的GPU输出.

```c++
void SourcePlayer::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
    // 原始帧交给windowRender里的vulkan处理
    windowRender->render(frame);
    // vulkan输出AHardwareBuffer，则直接转发
    GpuFrame vframe = frame;
#if __ANDROID__
    vframe.buffer = vrender->getOutGpuBuffer();
#endif
#if __APPLE__
    vframe.context = (IRenderContext*)vrender->getOutGpuBuffer();
#endif
    // GPU数据直接转发到编码源
    // vframe是vulkan处理后的数据
    // frame是如相机原始的OES纹理数据
    fRawSource->pushFrame(vframe);
}
```

在由IOSVEncoder编码时,先看是不是vulkan处理后的数据,如果是,先从IOSurfaceRef得到CVPixelBufferRef,再丢给VideoToolbox.如果不是,就直接丢给VideoToolbox.

```c++
DecodeResult IOSVEncoder::encode(const GpuFrame &frame) {
  if (!compressionSession) {
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }  
  if(frame.context != nullptr){
#if AVOX_ENABLE_VULKAN
    // 对应的是vkIosImage的数据
    // APPLE下,需要父类IRenderContext在前,不然后面转void*,再转vkiosimage指针会偏移
    // 用dynamic_cast也会直接返回空,打印的typeid的name又是正常的,暂时认为多继承顺序导致的
    VkIosImage* vkIosImage = dynamic_cast<VkIosImage*>(frame.context);
    if(!vkIosImage){
      return DecodeResult::dataError;
    }
    IOSurfaceRef iosurface = vkIosImage->getIOSurface();
    if (!iosurface ) {
        LOGFLF(LogLevel::error, "invalid or released IOSurface");
        return DecodeResult::dataError;
    }
    if (preSurface != iosurface) {
        preSurface = iosurface;
        if(pixelBuffer){
            CVPixelBufferRelease(pixelBuffer);
            pixelBuffer = nullptr;
        }
        // 将 IOSurfaceRef 转换为 CVPixelBufferRef
        CVReturn cvRet = CVPixelBufferCreateWithIOSurface(nullptr,iosurface,nullptr,&pixelBuffer);
        if (cvRet != kCVReturnSuccess || !pixelBuffer) {
            LOGFLF(LogLevel::warn, "Failed to create CVPixelBuffer from IOSurface:", cvRet);
            return DecodeResult::dataError;
        }
    }
    if(!pixelBuffer){
      return DecodeResult::dataError;
    }    
    // 使用 pixelBuffer 进行编码
    CMTime presentationTime = CMTimeMakeWithSeconds(frame.pts, 1000000);
    VTEncodeInfoFlags infoFlags = 0;
    OSStatus status = VTCompressionSessionEncodeFrame(compressionSession,pixelBuffer,
        presentationTime,kCMTimeInvalid,nullptr,nullptr,&infoFlags);  
    if (status != noErr) {
      LOGFLF(LogLevel::warn, "Failed to encode frame:", status);
      return DecodeResult::dataError;
    }
#endif    
  } else if (frame.buffer) {
   // 使用Metal渲染的GPU帧编码
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)frame.buffer;
    CMTime presentationTime = CMTimeMakeWithSeconds(frame.pts, 1000000);
    VTEncodeInfoFlags infoFlags = 0;
    OSStatus status = VTCompressionSessionEncodeFrame(
        compressionSession, pixelBuffer, presentationTime, kCMTimeInvalid,
        nullptr, nullptr, &infoFlags);
    if (status != noErr) {
      LOGFLF(LogLevel::warn, "failed to encode frame:", status);
      return DecodeResult::dataError;
    }
  }else{
      return DecodeResult::dataError;
  }
  return DecodeResult::success;
}
```

整个过程差不多就是这样,这样IOS硬解就支持二种输入了,一是相机原始的CVPixelBufferRef,二是处理后的vulkan的GPU结果.


