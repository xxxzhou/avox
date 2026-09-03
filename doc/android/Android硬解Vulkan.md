# Android硬解经AHardwareBuffer高效到Vulkan管线

在[播放器Android](https://zhuanlan.zhihu.com/p/1924537652897625121)篇里，集成Android原生硬解，以及把硬解的数据通过二个不同的EGLContext渲染出来，当时提过如果把要把硬解结果给Vulkan渲染，只能把硬解出来的数据通过AMediaCodec_getOutputBuffer函数map到内存中，然后提交到Vulkan管线，这种方式比较低效，在原来[android下vulkan与opengles纹理互通](https://zhuanlan.zhihu.com/p/302285687)把Vulkan的纹理直接输出到opengles上，本文介绍如何Android硬解出来的数据直接通过AHardwareBuffer对象映射到Vulkan的纹理中，不需要map到内存，这样就和Window平台一样，又能硬解，又能高效使用[Vulkan移植GPUImage总结](https://zhuanlan.zhihu.com/p/373137758)Vulkan图像计算管线。

## 输出到FBO

原[播放器Android](https://zhuanlan.zhihu.com/p/1924537652897625121)里数据直接输出到EGL窗口上，在这需要增加相应的逻辑，如果没有EGL窗口，其输出到FBO绑定的纹理上。

在[播放器Android](https://zhuanlan.zhihu.com/p/1924537652897625121)里没有initContext的实现，这个还是比较重要的，其几个参数不同对应不同情况，如sharedCtx没值，表明当前自己是主EGL上下文，比如解码线程需要自己的EGL上下文，这里就输入空，而在窗口呈现线程，需要共享解码EGL上下文里解码出来的OES纹理，则传入解码的EGL上下文。而bSelfRender表明自身需要初始EGL上下文，在解码线程与窗口呈现线程，这二个都是true，而在UE/Unity的渲染线程里，可以直接使用已经提供的EGL上下文直接使用opengl函数，那么就传入false。而window表明传入的窗口，如果是EGL窗口，则在android上层对应Surface，JNI对应ANativeWindow，而现在，经Vulkan图像管线后输出到Vulkan窗口，则这里传入空，使用eglCreatePbufferSurface离屏渲染。

``` C++
void GLESContext::initContext(EGLContext sharedCtx, bool bSelfRender,
                              EGLNativeWindowType window) {
  unInit();
  shardCtx = sharedCtx;
  selfRender = bSelfRender;
#ifdef __ANDROID__
  // 如果没有外部上下文,并且需要自己渲染,则创建独立的上下文
  if (selfRender) {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
      LOGFLF(LogLevel::warn, "eglGetDisplay failed");
      return;
    }
    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    if (!eglInitialize(display, &majorVersion, &minorVersion)) {
      return;
    }
    EGLint numConfigs = 0;
    EGLint attribList[] = {EGL_RED_SIZE,8,
                           EGL_GREEN_SIZE,8,
                           EGL_BLUE_SIZE,8,
                           EGL_ALPHA_SIZE,8,
                           EGL_DEPTH_SIZE,8, 
                           EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                           EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
                           EGL_NONE};
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    if (!eglChooseConfig(display, attribList, &config, 1, &numConfigs)) {
      LOGFLF(LogLevel::warn, "eglChooseConfig failed");
      return;
    }
    if (numConfigs < 1) {
      LOGFLF(LogLevel::warn, "eglChooseConfig failed, no valid config found");
      return;
    }
    if (shardCtx) {
      EGLint eglVersion = 0;
      if (!eglQueryContext(display, shardCtx, EGL_CONTEXT_CLIENT_VERSION,
                           &eglVersion)) {
        AVOX_EGL_LOG("eglQueryContext failed.");
      } else {
        contextAttribs[1] = eglVersion;
      }
    }
    selfCtx = eglCreateContext(display, config, shardCtx, contextAttribs);
    if (selfCtx == EGL_NO_CONTEXT) {
      log(LogLevel::warn, "eglCreateContext failed");
      return;
    }
    LOGFLF(LogLevel::info, "init self context");
    if (window) {
      surface = eglCreateWindowSurface(display, config, window, nullptr);
    } else {
      const EGLint pBufferAttrs[] = {
          EGL_WIDTH,          imageFormat.width,  EGL_HEIGHT,
          imageFormat.height, EGL_TEXTURE_TARGET, EGL_NO_TEXTURE,
          EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE,     EGL_NONE};
      surface = eglCreatePbufferSurface(display, config, pBufferAttrs);
    }
    int32_t width = 0;
    int32_t height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    if (width > 0) {
      imageFormat.width = width;
    }
    if (height > 0) {
      imageFormat.height = height;
    }
    bool bmake = makeCurrent();
    if (!bmake) {
      AVOX_EGL_LOG("eglMakeCurrent failed.");
    }
  } else {
    // 当不需要自建上下文时，获取当前线程的EGL环境
    display = eglGetCurrentDisplay();
    selfCtx = eglGetCurrentContext();
    surface = eglGetCurrentSurface(EGL_DRAW);
    if (selfCtx == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE) {
      LOGFLF(LogLevel::warn, "Current thread has no valid EGL context");
      return;
    }
    // 继承当前上下文的版本信息
    EGLint eglVersion = 0;
    eglQueryContext(display, selfCtx, EGL_CONTEXT_CLIENT_VERSION, &eglVersion);
    LOGFLF(LogLevel::info, "Using existing EGL context version:", eglVersion);
  }
  LOGFLF(LogLevel::info, "init context");
#endif
}
```

原GLPipeGraph负责把NV12纹理呈现到窗口，而现在需要输出到FBO上，在initGraph检查添加无窗口的情况，添加FBO以及绑定相应纹理，以及渲染到FBO。

``` C++
void GLPipeGraph::initGraph() {
  if (!decodeCtx) {
    log(LogLevel::warn, "initGraph failed, decodeCtx is null");
    return;
  }
  // 如果是EGL窗口，则应该有值，如果Vulkan窗口，则需要有ImageFormat
  if (!window && (imageFormat.width == 0 || imageFormat.height == 0)) {
    return;
  }
  // 无窗口，输出到共享buffer
  if (!window) {
    LOGFLF(LogLevel::info, "window is null");
  }
  closeProgram();
  // 渲染使用decodeCtx解码上下文做共享
  // 这样可以使用解码后的OES纹理
  // window如果为空,则表明渲染到FBO对应的AHardwareBuffer上
  initContext(decodeCtx, true, window);
  createProgram();
#ifdef __ANDROID__
  // if (!window) {
  //   sharedBuffer = std::make_unique<SharedGpuBuffer>();
  //   sharedBuffer->createAndroidBuffer(imageFormat);
  //   sharedBuffer->bindGL(textureId);
  //   LOGFLF(LogLevel::info, "create shardbuffer success");
  // }
#endif
  LOGFLF(LogLevel::info, "success");
}
void GLPipeGraph::createProgram() {
#ifndef WIN32
  if (glProgram == 0) {
    glProgram = createGLProgram(VertexShaderString, FragmentShaderString);
  }
  if (glProgram == 0) {
    AVOX_GL_LOG("create gl program failed");
    return;
  }
  // 创建RGBA纹理
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageFormat.width, imageFormat.height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  // 生成FBO
  glGenFramebuffers(1, &fboId);
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textureId, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  posAttr = glGetAttribLocation(glProgram, "position");
  uvAttr = glGetAttribLocation(glProgram, "uv");
  extAttr = glGetUniformLocation(glProgram, "oes_texture");
  LOGFLF(LogLevel::info, "create program success,textureId:", textureId);
#endif
}
void GLPipeGraph::useProgram(uint32_t extId) {
...
  // 如果没有设置窗口，则渲染到FBO上
  if (!window) {
    glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  }
  glViewport(0, 0, imageFormat.width, imageFormat.height);
  // glClearColor(1, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // 激活纹理
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, extId);
  glUniform1i(extAttr, 0);
  // 设置可编程管线参数
  glUseProgram(glProgram);
  glEnableVertexAttribArray(posAttr);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void *)(verts));
  glEnableVertexAttribArray(uvAttr);
  glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void *)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  //
  glDisableVertexAttribArray(posAttr);
  glDisableVertexAttribArray(uvAttr);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  // 如果没有设置窗口，则渲染到FBO上
  if (!window) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
...
}
```

这样在调用useProgram时，如果Mediaplayer绑定的是EGL窗口，则直接输出到窗口上，如果没有窗口，则输出到FBO绑定的纹理上。

需要注意，这里中转的opengl纹理我用的RGBA纹理，而不是原始的OES纹理，主要有二个方面的考虑，一是兼容原来的EGL窗口渲染，二是OES纹理，其opengl/AHardwareBuffer/vulkan的实现会复杂些，这个流程先以简单的方式打通，后面看情况考虑是否有必要。

## EGL绑定AHardwareBuffer

原来aoce里的代码[HardwareImage](https://github.com/xxxzhou/aoce/blob/master/code/aoce_vulkan/android/HardwareImage.hpp),我重新整理了下，把EGL与AHardwareBuffer抽离出来。

``` C++
class SharedGpuBuffer {
public:
  SharedGpuBuffer(/* args */);
  ~SharedGpuBuffer();

protected:
  AHardwareBuffer *hardwareBuffer = nullptr;
  EGLImageKHR eglImage = nullptr;

  ImageFormat format = {};
  bool bSupport = false;
  
public:
  AHardwareBuffer *getHarderBuffer() { return hardwareBuffer; }
  const ImageFormat &getFormat() { return format; }

private:
  void bindEGL();
  void close();

public:
  void createAndroidBuffer(const ImageFormat &format);
  void bindGL(uint32_t textureId, uint32_t texType = GL_TEXTURE_2D);
  void release();

  void logData();

protected:
  virtual void onInit() {};
  virtual void onRelease() {};
};
SharedGpuBuffer::SharedGpuBuffer(/* args */) {
  bSupport = supportSharedGpuBuffer();
}

SharedGpuBuffer::~SharedGpuBuffer() { close(); }

void SharedGpuBuffer::createAndroidBuffer(const ImageFormat &format_) {
  // 释放已有的资源
  release();
  format = format_;
  AHardwareBuffer_Desc usage = {};
  // filling in the usage for HardwareBuffer
  usage.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  usage.height = format.height;
  usage.width = format.width;
  usage.layers = 1;
  usage.rfu0 = 0;
  usage.rfu1 = 0;
  // usage.stride = 0;
  //  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
  usage.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
                AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
#if __ANDROID_API__ >= 26
  AHardwareBuffer_allocate(&usage, &hardwareBuffer);
  bindEGL();
  onInit();
#endif
}

void SharedGpuBuffer::bindEGL() {
  // android绑定AHardwareBuffer与egl image
  EGLClientBuffer eglbuffer = eglGetNativeClientBufferANDROID(hardwareBuffer);
  if (!eglbuffer) {
    log(LogLevel::warn, "eglGetNativeClientBufferANDROID failed");
    return;
  }
  EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  eglImage =
      eglCreateImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, eglbuffer, attrs);
  // assert(image != EGL_NO_IMAGE_KHR);
  if (!eglImage) {
    int32_t errorId = eglGetError();
    log(LogLevel::warn, "not create hardware image,error id", errorId);
  } else {
    log(LogLevel::info, "hardware image create success.");
  }
}

void SharedGpuBuffer::bindGL(uint32_t textureId, uint32_t texType) {
  if (!eglImage) {
    log(LogLevel::warn, "eglImage is null");
    return;
  }
  int bindType = GL_TEXTURE_2D;
  if (texType > 0) {
    bindType = texType;
  }
  glBindTexture(bindType, textureId);
  glEGLImageTargetTexture2DOES(bindType, eglImage);
  glBindTexture(bindType, 0);
}

void SharedGpuBuffer::close() {
  LOGFLF(LogLevel::info, " harder/egl buffer");
  if (hardwareBuffer != nullptr) {
#if __ANDROID_API__ >= 26
    AHardwareBuffer_release(hardwareBuffer);
#endif
    hardwareBuffer = nullptr;
  }
  if (eglImage) {
    eglDestroyImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), eglImage);
    eglImage = nullptr;
  }
}

void SharedGpuBuffer::release() {
  close();
  onRelease();
}

void SharedGpuBuffer::logData() {
  if (!hardwareBuffer) {
    return;
  }
  // 如何验证hardwareBuffer里数据？验证是有数据的
  void *mappedData = nullptr;
  const int lockFlags = AHARDWAREBUFFER_USAGE_CPU_READ_RARELY;
  int ret =
      AHardwareBuffer_lock(hardwareBuffer, lockFlags, -1, nullptr, &mappedData);
  if (ret == 0 && mappedData) {
    AvoxData data = {};
    data.data = (uint8_t *)mappedData + 12300;
    data.size = 100;
    log(LogLevel::info, "hardwareBuffer data:", data);
    AHardwareBuffer_unlock(hardwareBuffer, nullptr);
  } else {
    log(LogLevel::error, "AHardwareBuffer_lock failed:", ret);
  }
}
```

通过这个SharedGpuBuffer这个类，可以把opengles 纹理与AHardwareBuffer显存映射到一块位置上。

## AHardwareBuffer绑定VkImage

如何把AHardwareBuffer与VkImage显存映射在同一位置了？

``` C++
// https://developer.android.com/ndk/reference/group/a-hardware-buffer
class HardwareImage : public SharedGpuBuffer, public VkContextRef {
public:
  HardwareImage(/* args */);
  virtual ~HardwareImage();

private:
  VkImage vkImage = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

public:
  inline VkImage getImage() { return vkImage; }

private:
  // https://android.googlesource.com/platform/cts/+/master/tests/tests/graphics/jni/VulkanTestHelpers.cpp
  // 简化与兼容，限定都用RGBA格式
  void bindVK();

public:
  virtual void onInit() override;
  virtual void onRelease() override;
};
HardwareImage::HardwareImage(/* args */) {}

HardwareImage::~HardwareImage() { onRelease(); }

void HardwareImage::onInit() { bindVK(); }

void HardwareImage::onRelease() {
  if (vkImage) {
    vkDestroyImage(vkDevice, vkImage, nullptr);
    vkImage = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
}

// https://android.googlesource.com/platform/cts/+/master/tests/tests/graphics/jni/VulkanTestHelpers.cpp
void HardwareImage::bindVK() {
  AHardwareBuffer_Desc bufferDesc = {};
#if __ANDROID_API__ >= 26
  AHardwareBuffer_describe(hardwareBuffer, &bufferDesc);
#else
  bufferDesc.width = format.width;
  bufferDesc.height = format.height;
  bufferDesc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  // AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
  bufferDesc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
                     AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
#endif
  VkAndroidHardwareBufferFormatPropertiesANDROID formatInfo = {
      .sType =
          VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = nullptr,
  };
  VkAndroidHardwareBufferPropertiesANDROID properties = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &formatInfo,
  };
  vkGetAndroidHardwareBufferPropertiesANDROID(vkDevice, hardwareBuffer,
                                              &properties);
  VkExternalFormatANDROID externalFormat{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
      .pNext = nullptr,
      .externalFormat = formatInfo.externalFormat,
  };
  VkExternalMemoryImageCreateInfo externalCreateInfo{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .handleTypes =
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
  };
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalCreateInfo;
  imageInfo.flags = 0u;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = formatInfo.format;
  imageInfo.extent = {
      bufferDesc.width,
      bufferDesc.height,
      1u,
  };
  imageInfo.mipLevels = 1u, imageInfo.arrayLayers = 1u;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.queueFamilyIndexCount = 0;
  imageInfo.pQueueFamilyIndices = nullptr;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage),
                 "create image failed");

  VkImportAndroidHardwareBufferInfoANDROID androidHardwareBufferInfo{
      .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
      .pNext = nullptr,
      .buffer = hardwareBuffer,
  };
  VkMemoryDedicatedAllocateInfo memoryAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &androidHardwareBufferInfo,
      .image = vkImage,
      .buffer = VK_NULL_HANDLE,
  };
  // android的hardbuffer位置(properties)
  VkMemoryRequirements
    requires;
  vkGetImageMemoryRequirements(vkDevice, vkImage, &requires);
  uint32_t memoryTypeIndex = 0;
  bool getIndex = wphyDevcie->getMemoryTypeIndex(properties.memoryTypeBits, 0,
                                                 memoryTypeIndex);
  assert(getIndex);
  VkMemoryAllocateInfo memoryInfo = {};
  memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryInfo.pNext = &memoryAllocateInfo;
  memoryInfo.memoryTypeIndex = memoryTypeIndex;
  memoryInfo.allocationSize = properties.allocationSize;
  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &memory),
                 "allocate memory failed");

  VkBindImageMemoryInfo bindImageInfo = {};
  bindImageInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
  bindImageInfo.pNext = nullptr;
  bindImageInfo.image = vkImage;
  bindImageInfo.memory = memory;
  bindImageInfo.memoryOffset = 0;
  AVOX_VULKAN_LOG(vkBindImageMemory2KHR(vkDevice, 1, &bindImageInfo),
                 "bind image memory failed");
}
```

## 组合流程

在[播放器FFmpeg](https://zhuanlan.zhihu.com/p/1924537408311001536)里，有VkVideoRender使用软解数据渲染以及使用Windows平台硬解的DX11纹理渲染，那在这里，完善Android硬解出来的数据。

``` C++
class VkVideoRender : public VideoRender,
                      public RunTask,
                      public IVOutputLayerOb {
 VCodecTh codecTH = VCodecTh::other;
#ifdef WIN32
  IRenderContext *dx11Context = nullptr;
#endif
#ifdef __ANDROID__
  std::unique_ptr<GLPipeGraph> glPipeGraph = nullptr;
#endif
}
void VkVideoRender::start() {
    ...
if (videoDecoder) {
    codecTH = videoDecoder->getCodecTh();
    LOGFLF(LogLevel::info, "codecTh:", (int32_t)codecTH);
#ifdef __ANDROID__
    if (codecTH == VCodecTh::androidMC) {
      glPipeGraph = std::make_unique<GLPipeGraph>();
      auto *andDecoder = dynamic_cast<AndVDecoder *>(videoDecoder);
      if (andDecoder) {
        glPipeGraph->updateImageFormat(andDecoder->getImageFormat());
        glPipeGraph->setDecodeContext(andDecoder->getContext());
      }
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
    log(LogLevel::info, "android harder decoder,user vk render");
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
        HwVideoBuffer *hwBuffer =
            dynamic_cast<HwVideoBuffer *>(frame->buffer.get());
        if (hwBuffer && hwBuffer->getRenderContext()) {
          GLESContext *glesContext =
              dynamic_cast<GLESContext *>(hwBuffer->getRenderContext());
          if (glPipeGraph) {
            // 解码队列的数据压入到OES纹理
            frame->release(true);
            // 把OES纹理转换成RGBA纹理,并共享给AHarderBuffer
            glPipeGraph->useProgram(glesContext->getImage());
            // AHarderBuffer纹理绑定Vkimage
            inputLayer->get()->inputGpuData(glPipeGraph.get());
            // 运行管线
            graph->run();
          }          
        }
      }
#endif 
    }
  }
}
```

输入层VkInputLayer添加相应逻辑。

``` C++
class VkInputLayer : public VInputLayer, public VkLayer {
 #ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<HardwareImage> hardwareImage = nullptr;
  uint32_t textureId = 0;
  bool bAndInterop = false;
#endif   
}

void VkInputLayer::inputGpuData(IRenderContext *context) {
  bGpuInput = true;
  ImageFormat imageFormat = inFormats[0];
#ifdef WIN32
  // 根据DX11/DX12的输入
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    RenderType renderType = context->getRenderType();
    ImageFormat newFormat = {};
    if (renderType == RenderType::D3D11) {
      IDx11Context *contextDX11 = (IDx11Context *)context;
      ID3D11Texture2D *dxtexture = contextDX11->getTexture();
      if (!dxtexture) {
        return;
      }
      getImageFormat(dxtexture, newFormat);
    } else if (renderType == RenderType::D3D12) {
      IDx12Context *contextDX12 = (IDx12Context *)context;
      ID3D12Resource *dxtexture = contextDX12->getTexture();
      if (!dxtexture) {
        return;
      }
      getImageFormat(dxtexture, newFormat);
    }
    setLayerFormat(this, newFormat);
  }
  // 管线资源已经准备完成
  if (vkPipeGraph->resourceReady()) {
    // 把另外线程的GPU资源复制到共享纹理中
    if (winImage && winImage->getInit()) {
      winImage->dx11CopyTemp(context);
    }
  }
#endif
#ifdef __ANDROID__
  GLESContext *glesContext = dynamic_cast<GLESContext *>(context);
  if (imageFormat.width == 0 || imageFormat.height == 0) {
    imageFormat = glesContext->getImageFormat();
    if (imageFormat.imageType == ImageType::other) {
      imageFormat.imageType = ImageType::rgba8;
    }
    textureId = glesContext->getImage();
    log(LogLevel::info,
        "VkInputLayer::inputGpuData gles context textureId:", textureId,
        " width:", imageFormat.width, " height:", imageFormat.height,
        " image type:", getImageTypeStr(imageFormat.imageType));
    setLayerFormat(this, imageFormat);
  }
#endif
}
void VkInputLayer::onInitGraph() {
#ifdef WIN32
  winImage = std::make_unique<VkWinImage>();
  winImage->setVkContext(vkPipeGraph);
  bWinInterop = wphyDevcie->bInterpDx11();
#elif __ANDROID_API__ >= 26
  bAndInterop = wphyDevcie->bInterpAndroid() && supportSharedGpuBuffer();
  if (bAndInterop) {
    hardwareImage = std::make_unique<HardwareImage>();
    hardwareImage->setVkContext(vkPipeGraph);
  }
#endif
}
void VkInputLayer::onInitVkBuffer() {
#ifdef WIN32
  winImage->bindD3D(vkPipeGraph->getRenderType(), vkPipeGraph->getD3D11Device(),
                    inFormats[0]);
#endif
#ifdef __ANDROID__
  LOGFLF(LogLevel::info, "bAndInterop:", bAndInterop);
  if (bAndInterop) {
    hardwareImage->createAndroidBuffer(outFormats[0]);
    hardwareImage->bindGL(textureId);
    LOGFLF(LogLevel::info,
           "create android shardbuffer success,textureId:", textureId,
           " width:", outFormats[0].width, "height:", outFormats[0].height);
  }
#endif
}
```

注意在这，opengles rgba纹理/AHradwareBuffer/VkImage并没有跨线程，而opengles rgba/opengles oes这是跨线程了的，其opengles rgba纹理是在Vk运行管线里线程生成的EGL上下文，所以也不需要考虑同步问题。