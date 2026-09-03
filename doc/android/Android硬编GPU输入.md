# Android MediaCodec GPU编码实践

集成ffmpeg的软编后,在windows/android/ios/linux都很容易适配,不过硬编和硬解一样,不同的平台原生实现差别还是比较大,记录下在android平台硬编的实现.

如果是传入NV12的CPU数据,网上有不少实现,让AI写也能快速实现,但是如果是[NdkCamera2使用OES纹理渲染](NdkCamera2使用OES纹理.md)编码相机直出的OES纹理就有些麻烦了,更进一步,相机数据处理后,如加入AI识别,字体渲染,大小变化后的GPU数据直接给硬编,就更复杂了,本文记录了如何使用android原生硬编对接GPU输入.

## OES纹理输入

我最开始查看MediaCodec里的编码API时,看到AMediaCodec_configure里支持直接输入Surface,就如NdkCamera里使用SurfaceTexture做为输出队列一样,自然就想到把这个SurfaceTexture的nativewindow传给MediaCodec,但是这样会提示surface已经连接,这个好理解,直出的OES纹理,我需要使用EGL连接SurfaceTexture处理成RGBA数据渲染,于是尝试第二步,新建立一个SurfaceTexture,把OES纹理先输入到这个SurfaceTexture然后再对接MediaCodec,发现还是不行,提示说没有输出数据,后来想了下,前面相机/解码对接SurfaceTexture时,把SurfaceTexture当作队列,对于相机/解码是生产端,对于开发来说是消费端,而在编码时,SurfaceTexture对于开发来说,应该是生产端,而SurfaceTexture应该是不支持这种操作的,所以从别的地方入手.

然后AMediaCodec_createInputSurface这个API,看说明是configure后,start前生成一个surface做为输入队列,用来存入生成的数据,那么主要问题就是如何把OES纹理输入到这个surface了,经尝试如下方式成功实现.

```c++
DecodeResult AndVEncoder::encode(const GpuFrame& frame) {
  if (!mediaCodec) {
    // 告诉编码器以surface方式
    bGpu = true;
    eglVideoYuv = std::make_unique<EglVideoYuv>();
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 原始GPU数据渲染到surface上
  eglVideoYuv->renderFrame(frame);
  // 使用surface作为输入时，不能使用queueInputBuffer  
  return encode();
}
bool EglVideoYuv::vaildAndInitGraph(const GpuFrame& frame) {
  if (glProgram) {
    return true;
  }
  inContext = dynamic_cast<GLESContext*>(frame.context);
#ifdef __ANDROID__
  hardwareBuffer = (SharedGpuBuffer*)(frame.buffer);
#endif
  LOGFLF(LogLevel::info, "inContext:", inContext);
  if (!inContext) {
    LOGFLF(LogLevel::warn, "eglContext is null");
    return false;
  }
  // 如果输入的EGL上下文是本身,则直接用
  // 否则用传入的EGL上下文作为共享上下文
  // 不管如何,当前EGL环境都可以访问传入的GPU数据
  initContext(inContext->getContext());
  eglSize.width = frame.format.width;
  eglSize.height = frame.format.height;
#ifdef __ANDROID__
  // surface是android原生编码的输入,需要把数据输出到对应的surface
  createSurface(surface);
#endif
  createProgram();
  return true;
}
void EglVideoYuv::renderGpuFrame(const GpuFrame& frame) {
  if (!glProgram) {
    return;
  }
  uint32_t inTexId = inContext->getImage();
  if (inTexId == 0) {
    return;
  }
  // 指定当前编码的Surface
  makeCurrent();
  // 设置Surface时间戳 单位微秒,编码器需要时间戳
  eglPresentationTimeANDROID(display, eglsurface, frame.pts * 1000);
  glViewport(0, 0, eglSize.width, eglSize.height);
  glClear(GL_COLOR_BUFFER_BIT);
  // 激活纹理
  glActiveTexture(GL_TEXTURE0);
  // 如果是VkImage映射的纹理,则绑定对应textureId
  if (hardwareBuffer) {
      hardwareBuffer->renderGL(textureId,GL_TEXTURE_2D);
  } else {
    // 如果是OES纹理
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, inTexId);
  }
  // 设置可编程管线参数
  glUseProgram(glProgram);
  glUniform1i(extAttr, 0);
  glEnableVertexAttribArray(posAttr);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void*)(verts));
  glEnableVertexAttribArray(uvAttr);
  glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  
  glDisableVertexAttribArray(posAttr);
  glDisableVertexAttribArray(uvAttr);
  if (hardwareBuffer) {
    glBindTexture(GL_TEXTURE_2D, 0);
  } else {
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  }
  // 则交换缓冲区
  eglSwapBuffers(display, eglsurface);
  unMakeCurrent();
}
```

简单来说,把AMediaCodec_createInputSurface生成的surface,由当前EGL环境生成对应的eglSurface,然后把OES纹理渲染到这个eglSurface上,这样surface做为一个输入队列,就可以把OES纹理数据输入到这个队列里了.

需要注意的是,如果是surface输入,不能使用AMediaCodec_queueInputBuffer,因为队列相关全由对应的surface自动管理,外部操作会引起问题,但是AMediaCodec_queueInputBuffer可以传入当前编码的时间戳,没有正确的时间戳,编码器和封装流都会报错,对应surface的时间戳,需要使用eglPresentationTimeANDROID来设置.

## Vulkan处理后的GPU数据

如果需要对相机出的OES处理后,比如说的加入AI识别,字体渲染,改分辨率等操作后,如何把处理后的GPU数据输入到硬编器里呢?

当前项目各平台原生渲染(dx11/dx12/opengles/metal)只是实现yuv/rgba的相互转化,不同平台原生渲染实现差异大,各种图像处理如果用原生的,都要各实现一次,如果实现一个统一的RHI,对个人来说,工作量与难度都太大,于是选择所有平台的图像处理都是由vulkan完成,并实现各原生渲染的GPU数据与vulkan的GPU数据相互映射.

前置需要了解[Android硬解经AHardwareBuffer高效到Vulkan管线](https://zhuanlan.zhihu.com/p/1933240978434679422),清楚当前是如何Opengles与vulkan如何通过AHardwareBuffer进行相互映射的.

先看三种数据来源,一是处理后map到cpu的nv12数据,二是原生OES纹理,三是处理后的vulkan的GPU结果.

```c++
void SourcePlayer::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
  // log(LogLevel::info, "onGpuFrame:", frame.pts);
  // HighClock clock = {};
  windowRender->render(frame);  
  if (!fRawSource || !vrender) {
    return;
  }
  if (vrender->bCpuOut()) {
    // 输出CPU数据,需要把当前渲染结果的NV12数据取出来
    YUVFrame yframe = {};
    bool bGet = vrender->getCpuFrame(yframe);
    if (bGet) {
      yframe.pts = frame.pts;
      yframe.dts = frame.dts;
      fRawSource->pushFrame(yframe);
    }
  } else {
    // vulkan输出AHardwareBuffer，则直接转发
    GpuFrame vframe = frame;
    vframe.buffer = vrender->getOutGpuBuffer();
    // GPU数据直接转发到编码源
    // vframe是vulkan处理后的数据
    // frame是如相机原始的OES纹理数据    
    fRawSource->pushFrame(vframe);
  }
}
void* VkVideoRender::getOutGpuBuffer() {
  if (outputLayer) {
    return outputLayer->get()->getOutGpuBuffer();
  }
  return nullptr;
}
void* VkOutputLayer::getOutGpuBuffer() {
#ifdef __ANDROID__
  if (vkAndImage) {
    return vkAndImage.get();
  }
#endif
  return nullptr;
}
```

当图像处理结束后,结果保存在VkOutputLayer,里面的vkAndImage可以看[Android硬解经AHardwareBuffer高效到Vulkan管线](https://zhuanlan.zhihu.com/p/1933240978434679422),里面就是一个AHardwareBuffer,分别映射vkImage与gltexture,不管是vkImage/gltexture作为输入,在另一边gltexture/vkImage都能得到对方处理的结果.

因此在上面的EglVideoYuv::renderGpuFrame,会首先查看当前frame的buffer是否有值,如果有值,则说明加了vulkan处理的数据,buffer就是处理后的vkimage映射封装AHardwareBuffer对象,可以直接当做一个RGBA的opengles纹理来渲染.

``` C++
void EglVideoYuv::createProgram() {
  if (glProgram == 0) {
    std::string fragmentStr = fragmentShaderSource;
#ifdef __ANDROID__
    if (hardwareBuffer) {
      fragmentStr = bufferFragmentShaderSource;
    }
#endif
    glProgram = createGLProgram(vertexShaderSource, fragmentStr.c_str());
  }
  if (glProgram == 0) {
    AVOX_GL_LOG("create gl program failed");
    return;
  }
  posAttr = glGetAttribLocation(glProgram, "position");
  uvAttr = glGetAttribLocation(glProgram, "uv");
  extAttr = glGetUniformLocation(glProgram, "oes_texture");
#ifdef __ANDROID__
  // hardwareBuffer对应vulkan处理后的GPU的RGBA数据
  // 绑定一个相应的纹理上,通过hardwareBuffer,映射vkimage->gltexture
  if (hardwareBuffer) {
    extAttr = glGetUniformLocation(glProgram, "texture2d");
    // 创建RGBA纹理,vulkan对应hardwareBuffer是RGBA格式
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, eglSize.width, eglSize.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    hardwareBuffer->bindGL(textureId, GL_TEXTURE_2D);
  }
#endif
}
```

这样Android的原生硬解,就支持三种输入了,一是相机原始的OES纹理,二是图像处理后map到cpu的nv12数据,三是处理后的vulkan的GPU结果.




