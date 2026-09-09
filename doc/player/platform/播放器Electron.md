# 基于C++与NodeJs的Electron播放器Demo

如下是现在效果图:

[视频](../../assets/video/avc_electron.mp4)

不知是不是因为视频包含B帧，在WebRTC下，使用Edge浏览器查看，视频会前后帧错乱跳着，本项目播放会丢掉帧，但是保证了前后顺序。

前端最喜欢用的还是JS，因此本文主要研究如何使用JS调用当前播放器项目，记录遇到的问题。

主要流程如下:

1. 使用swig把C++接口封装给nodejs使用，使用Electron通过nodejs调用C++API。
2. 通过Electron里的BrowserWindow对象，对接底层Vulkan/DX11窗口呈现。
3. JS/CSS编写测试界面，测试底层播放器功能。

这种方式优缺点都很明显，最开始我是想把底层的最后渲染结果通过类似进程共享纹理，如DX11里的纹理共享NT句柄，通过浏览器WebGPU/WebGL调用共享句柄渲染，但是发现这种方式没有，或者有，但是我没找到，如果MAP到CPU然后给浏览器渲染，那硬解就没意义了，因此退一步，使用浏览器JS框架主要是方便前端界面，在这之外，播放窗口的渲染用Dx11/Vulkan，这就是现在的使用方式，不能在纯浏览器里使用，只能如Electron提供底层窗口句柄的方式使用，但是这种方式就和原生窗口一样，没有任何限制，如wasm方案对多线程的限制，浏览器对CPU使用率的这些限制，支持各种H265硬解，本地媒体文件，RTSP/RTMP等常见流媒体协议都支持，想扩充自己的私有协议也比较方便，只需要在C++层实现自己的IO解析就行。

## C++接口封装给nodejs使用

和前面使用swig把C++接口包给java/C#一样，这里先使用swig把C++接口封装给nodejs能使用的接口，如下这段主要是把.i文件生成相应的JAVASCRIPT_wrap.cxx，在这并不用CMake来生成nodejs封装模块，因为在CMake里填充node的三方库或头文件比较麻烦，node使用node-gyp专门生成C++相应的nodejs封装模块。

``` CMake
if(AVOX_ENABLE_SWIG_NODEJS AND WIN32)
    message(STATUS "SWIG Node.js enabled, generating Node.js bindings...")
    # 添加宏定义 
    list(APPEND SWIG_DEFINITIONS -DAVOX_NODEJS)       
    message(STATUS "SWIG Node.js SWIG_DEFINITIONS: ${SWIG_DEFINITIONS}")
    # 设置Node.js模块输出目录
    set(NODEJS_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nodejs/files") 
    set(CMAKE_SWIG_OUTDIR ${NODEJS_OUTPUT_DIR})      
    # 配置SWIG参数
    set_source_files_properties(${WRAPPERLIST} PROPERTIES USE_TARGET_INCLUDE_DIRECTORIES ON PLUSPLUS ON)       
    # 清空之前的CMAKE_SWIG_FLAGS
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -node)    
    # 生成包装代码
    add_custom_command(
        OUTPUT ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx
        COMMAND ${SWIG_EXECUTABLE} -javascript -node -c++ -I"${CMAKE_SOURCE_DIR}/src" -o ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx ${WRAPPERLIST}
        DEPENDS ${WRAPPERLIST}
        COMMENT "Generating SWIG wrapper for Node.js")
    # add_custom_target(avox_nodejs_swig ALL DEPENDS ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx)
endif()
```

在上面生成commonJAVASCRIPT_wrap.cxx包装代码后，编写编译包装代码的binding.gyp文件，主要是指明.cxx目录，链接头文件目录以及库路径，输出目录。

``` json
{
  "targets": [{
    "target_name": "avox_js",
    "sources": [
      "files/commonJAVASCRIPT_wrap.cxx"
    ],    
    "include_dirs": [      
      "../../src"
    ],    
    "libraries": [      
      "../../../build/windows/avox/install/AMD64/Debug/avox",
    ],
    "product_dir": "../../../build/windows/avox/install/AMD64/Debug", 
  }]
}
```

在binding.gyp目录下，使用node-gyp configure生成项目，因为需要给electron使用，因此需要electron-rebuild，对应自己安装的electron版本，如v31.7.7，调用node-gyp rebuild --target=v31.7.7 --arch=x64 --dist-url=https://electronjs.org/headers 编译项目，如果不用electron-rebuild，直接使用node-gyp build编译项目，在使用electron运行时，会提示node版本不对，一定要rebuild 指定当前node下的electron版本。

上面指定的product_dir输出目录，会指定生成avox_js.node文件，如果与引用的动态库在同一目录，就不需要指定环境变量路径了，如果编译提示fatal error LNK1103: 调试信息损坏或丢失: 无法查找或打开 PDB 文件，在生成目录删除avox_js.ipdb/avox_js.iobj文件再编译就行。

使用JS调用生成的avox_js.node文件，测试是否正常调用。

``` javascript
const avox = require('../../build/windows/avox/install/AMD64/Debug/avox_js.node')

var wind = avox.createVkWindow();
let params = new avox.WindowParamet();
wind.initWindow(params);
var mp = avox.createMediaPlayer();
mp.setWindow(wind);
mp.open("D://Back/美好_s16.mp4");
wind.run();
// 使用 setInterval 模拟循环
const intervalId = setInterval(() => {
    avox.logMsg(1,"hello world");
}, 1000);

avox.logMsg(1,"hello world");
```

使用node运行这个test.js文件，在其终端里会输出底层C++输出的各种日志，这样就表明可以正常调用了。

# Electron窗口对接底层窗口

查找各种可能把底层渲染纹理如何共享给网页的canvas的方案，当时AI给了种通过DX11共享NT句柄的方式，用WebGPU导入NT句柄渲染出来的方案，当时还兴奋了下，后面发现相应的API根本就没有，是AI自已编的。

后面在[实现steam game overlay(在electron环境下)](https://zhuanlan.zhihu.com/p/45438134)这篇文章里，发现Electron提供了BrowserWindow对象，可以直接访问当前平台的窗口句柄，如果有这个句柄，就可以直接生成dx11/vulkan的Swapchain，就和原生窗口流程一样，不需要做什么改动就能和原生窗口一样播放了，这种方案虽然达不到想要在BrowserWindow里渲染Canvas的这种方式，但是也算是达到部分目的，有没有大佬有更好的方案？

下面代码就是nodejs主线程里调用底层播放器的实现，主要的点就是拿到播放窗口的句柄，给Vk窗口生成Swapchain，指定父窗口为主窗口，监控主窗口的位置变化与播放Canvas的位置变化，保持播放窗口浮动在主窗口的Canvas指定位置上。

``` javascript
const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
// 加载原生模块
const avox = require('../../build/windows/avox/install/AMD64/Debug/avox_js.node')

let mainWindow;
let mediaWindow;
let nativeWindow;
let mediaPlayer;
let canvasRect = { x: 0, y: 0, width: 0, height: 0 };

app.on('ready', () => {
  mainWindow = new BrowserWindow({
    width: 800,
    height: 720,
    webPreferences: {
      // 允许渲染进程使用 Node.js
      nodeIntegration: true,
      // 允许直接调用 IPC（仅限测试环境）       
      contextIsolation: false
    }
  });
  // 创建vulkan/dx11渲染上层窗口
  mediaWindow = new BrowserWindow({
    parent: mainWindow,
    frame: false,
    // 这个选项一定要开,否则底层vulkan/dx11渲染的看不到
    transparent: true,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  });
  mediaWindow.loadFile('player.html');
  // 打开开发者工具
  // mediaWindow.webContents.openDevTools();
  const nativeHandle = mediaWindow.getNativeWindowHandle().readUInt32LE(0);
  // 创建native窗口
  nativeWindow = avox.createVkWindow();
  avox.initElectornWindow(nativeWindow, nativeHandle);
  mediaPlayer = avox.createMediaPlayer();
  mediaPlayer.setWindow(nativeWindow);
  // 加载页面
  mainWindow.loadFile('index.html');
  mainRect = mainWindow.getBounds();
  // 打开开发者工具
  mainWindow.on('move', () => {
    updateRect();
  });
  mainWindow.on('resize', () => {
    updateRect();
  });
});

ipcMain.handle('start', (event, url) => {
  if (!mediaPlayer) {
    return false;
  }  
  // D://Back/美好_s16.mp4  
  // D://Back/tt.mp4
  // rtsp://192.168.1.100/live/test
  mediaPlayer.open(url);
  nativeWindow.run();
  return true;
});

ipcMain.handle('stop', () => {
  if (!mediaPlayer) {
    return false;
  }
  mediaPlayer.stop();
  nativeWindow.stop();
  return true;
});

ipcMain.on('toggle-codec', (event, codec) => {
  avox.logMsg(0, `toggle-codec:${codec}`);
  if (!mediaPlayer) {
    return;
  }
  mediaPlayer.setHardDecode(codec);
});

ipcMain.on('set-io', (event, ioStr) => {
  // 使用ioPlan变量（需确保数组元素为枚举对象引用）
  var ioIndex = parseInt(ioStr, 10);
  avox.logMsg(0, `set-io:${avox.getIoPlanStr(ioIndex)}`);
  if (!mediaPlayer) {
    return;
  }
  mediaPlayer.setIoPlan(ioIndex);
});

ipcMain.on('set-watermark', (event, markIndex) => {
  avox.logMsg(0, `set-watermark:${markIndex}`);
  if (!mediaPlayer) {
    return;
  }
  mediaPlayer.getOption().setInt("watermarkIndex", markIndex);
});

ipcMain.on('set-lut', (event, lutIndex) => {
  avox.logMsg(0, `set-lut:${lutIndex}`);
  if (!mediaPlayer) {
    return;
  }
  mediaPlayer.getOption().setInt("lutIndex", lutIndex);
});

ipcMain.on('canvas-position-updated', (event, rect) => {
  avox.logMsg(0, `canvas-position:${rect.x},${rect.y},${rect.width},${rect.height}`);
  canvasRect = rect;
  updateRect();
});

function updateRect() {
  if (!mediaWindow) {
    return;
  }
  // 主窗口的位置,getContentBounds是HTML元素起点位置，而getBounds窗口位置
  const contentBounds = mainWindow.getContentBounds();
  // 更新 mediaWindow 的位置和大小
  const mediaLeft = contentBounds.x + canvasRect.x;
  const mediaTop = contentBounds.y + canvasRect.y;
  const maxWidth = Math.min(
    canvasRect.width,
    contentBounds.width - canvasRect.x - 10
  );
  const maxHeight = Math.min(
    canvasRect.height,
    contentBounds.height - canvasRect.y - 10
  )
  mediaWindow.setBounds({
    x: mediaLeft,
    y: mediaTop,
    width: maxWidth,
    height: maxHeight,
  });
}
```

当时还以为在BrowserWindow上不能指定HTML界面UI，后面发现不指定transparent: true的情况下，Vulkan渲染出来的图像并没有显示出来，因此猜测底层渲染出的图像是在BrowserWindow最底层，而BrowserWindow上层还会渲染HTML里的UI，那这样还可以在mediaWindow加载播放器控制的上层HTML的UI，这样的话，这种方案才有些实用的价值。

## 播放器功能测试

前面说过播放器使用DX11硬解有B帧的视频源时，会前后帧错乱跳，最开始以为是要自己排序，从输出的PTS上来看，确实是错乱的，但是根据PTS排序后，并没有解决，后面发现是硬解用的是DX11纹理数组，但是使用的当前纹理并不是按索引顺序输出的，比如纹理数组是20，在输出时，可能有一段时间一直在11/19/11/19二张索引上，这样你帧队列里保持的索引已经被覆盖了，不知是不是集成显卡的原因。解决方案也比较简单，生成一个自己的纹理数组，DX11每解码一帧出来，就按索引按顺序复制到自己的纹理数组上，这样就可以解决B帧错乱的问题。但是在WebRTC下，虽然时间不错乱了，但是感觉会丢那些顺序错误的帧，导致画面不流畅。

``` C++
void FFDx11Decoder::onFrame(AVFrame *avFrame, bool bDrop) {
  // codecCt有可能不是用的硬件解码器
  AVPixelFormat dx11Format = (AVPixelFormat)avFrame->format;
  if (dx11Format != AV_PIX_FMT_D3D11) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  // hwcontext_d3d11va.h
  ID3D11Texture2D *dx11Texture = (ID3D11Texture2D *)avFrame->data[0];
  if (!dx11Texture) {
    log(LogLevel::info, "dx11Texture is null");
    return;
  }
  D3D11_TEXTURE2D_DESC srcDesc;
  dx11Texture->GetDesc(&srcDesc);
  bool bCrateTexture = false;
  if (srcDesc.ArraySize > 1) {
    if (!copyTexture) {
      bCrateTexture = true;
    } else {
      D3D11_TEXTURE2D_DESC copyDesc;
      copyTexture->GetDesc(&copyDesc);
      if (copyDesc.Width != srcDesc.Width ||
          copyDesc.Height != srcDesc.Height) {
        bCrateTexture = true;
      }
    }
  }
  if (bCrateTexture) {
    device->CreateTexture2D(&srcDesc, NULL, &copyTexture);
    arraySize = srcDesc.ArraySize;
    index = 0;
  }
  int64_t queueIndex = reinterpret_cast<intptr_t>(avFrame->data[1]);
  if (srcDesc.ArraySize > 1) {
    d3dcontext->CopySubresourceRegion(
        copyTexture.Get(), D3D11CalcSubresource(0, index, srcDesc.MipLevels), 0,
        0, 0, dx11Texture,
        D3D11CalcSubresource(0, queueIndex, srcDesc.MipLevels), nullptr);
  }

  GpuFrame frame = {};
  frame.pts = avFrame->best_effort_timestamp;
  frame.dts = avFrame->pkt_dts;
  frame.format.width = avFrame->width;
  frame.format.height = avFrame->height;  
  frame.context = this;
  // 解码器并不是循环在用纹理数组，这样外面保存索引不对
  // 因为解码器可能一直在11/19/11/19二张索引上来回读写，这样就覆盖了
  if (srcDesc.ArraySize > 1) {
    frame.buffer = copyTexture.Get();
    frame.queueIndex = index;
    index = (index + 1) % arraySize;
  } else {
    frame.buffer = dx11Texture;
    frame.queueIndex = queueIndex;
  }
  // log(LogLevel::info, "array index:", queueIndex, " pts:", avFrame->pts,
  //     " dts:", avFrame->pkt_dts);
  dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
}
```

软硬解动态切换方案，看上面点击后，发现要等会，主要是要等到I帧，然后才能切换，因为非I帧，切换过去也解不了，有没大佬有更好的方案？

各种滤镜集成测试，之前说过，现在各平台硬解有二种呈现方案，一种是原生窗口，不同平台如windows/android/ios分别使用dx11/opengles/metal窗口显示，没有滤镜，因为分别写各自实现太麻烦，所以还有第二种方案，使用统一的Vulkan窗口呈现，在之前打通dx11/opengles/metal的GPU资源直接映射到Vulkan纹理后，不同平台使用统一的vulkan滤镜，下面是测试水印与LUT的测试代码。

``` C++
bool VkVideoRender::vaildAndInitGraph(const avox::VideoFrame &frame) {
  // 如果bResetFlag为true,则需要重新初始化
  if (graph && !bResetFlag) {
    return true;
  }
  if (!frame.buffer) {
    return false;
  }
  VkContext *ctx = nullptr;
  graph = std::make_unique<VkPipeGraph>(ctx);
  inputLayer = graph->addNode<VkInputLayer>();
  yuv2RGBA = graph->addNode<VkYUV2RGBALayer>();
  outputLayer = graph->addNode<VkOutputLayer>();
  std::string imagePath = getAvoxPath() + "/assets/images";
  log(LogLevel::info, "load image:", imagePath.c_str());
  if (watermarkIndex > 0) {
    // 加载水印图片
    blendImage = std::make_unique<ImageBuffer>();
    std::string waterName = watermarkIndex == 1 ? "blend.bmp" : "blend2.bmp";
    std::string blendPath = imagePath + "/" + waterName;
    loadBMP(blendPath.c_str(), blendImage.get());
    inputBlendLayer = graph->addNode<VkInputLayer>();
    inputBlendLayer->get()->inputCpuData(blendImage.get(), false);
    blendLayer = graph->addNode<VkBlendLayer>();
    blendParamet.centerX = 0.6;
    blendParamet.centerY = 0.2;
    blendParamet.width = 0.3;
    blendParamet.height = 0.3;
    blendParamet.alaph = 0.5;
    blendLayer->get()->updateParamet(blendParamet);
  }
  if (lutIndex > 0) {
    lutImage = std::make_unique<ImageBuffer>();
    std::string lutName =
        lutIndex == 1 ? "lookup_amatorka.bmp" : "lookup_miss_etikate.bmp";
    std::string lutPath = imagePath + "/" + lutName;
    loadBMP(lutPath.c_str(), lutImage.get());
    lutLayer = graph->addNode<VkLookupLayer>();
    lutLayer->get()->loadLookUp(lutImage->getPointer(),
                                lutImage->getBufferSize());
  }
  OutputParamet outputParamet = {};
  outputParamet.bCpu = false;
  outputParamet.bGpu = true;
  outputLayer->get()->updateParamet(outputParamet);
  // 输出
  outputLayer->get()->addObserver(this);
  bufferType = frame.buffer->getBufferType();
  std::shared_ptr<VkPipeNode> outNode = nullptr;
  if (bufferType == VBufferType::cpu) {
    outNode = inputLayer->addLine(yuv2RGBA);
  } else {
    outNode = inputLayer;
  }
  if (watermarkIndex > 0) {
    outNode = outNode->addLine(blendLayer);
    inputBlendLayer->addLine(blendLayer, 0, 1);
  }
  if (lutIndex > 0) {
    outNode = outNode->addLine(lutLayer);
  }
  outNode->addLine(outputLayer);
  bResetFlag = false;
  return true;
}
void VkVideoRender::onOptionChange(const char *key, ArgType option) {
  if (equalsIgnoreCase(key, "watermarkIndex")) {
    if (watermarkIndex != getInt(key)) {
      watermarkIndex = getInt(key);
      bResetFlag = true;
    }
  } else if (equalsIgnoreCase(key, "lutIndex")) {
    if (lutIndex != getInt(key)) {
      lutIndex = getInt(key);
      bResetFlag = true;
    }
  }
}
```

相应的日志表明各种滤镜组合测试。

``` txt
// 无任何效果,因为是GPU传递使用DX11的RGBA数据传递，所以没有启用YUV2RGBA层
[17:22:41.890] warn: PipeGraph.hpp:268 resetGraph (0)VkInputLayer
[17:22:41.890] warn: PipeGraph.hpp:268 resetGraph (1)VkYUV2RGBALayer
[17:22:41.890] warn: PipeGraph.hpp:268 resetGraph (2)VkOutputLayer
[17:22:41.890] info: PipeGraph.hpp:405 onValidNodes graph node from (0)VkInputLayer-0(rgba8) to (2)VkOutputLayer-0(rgba8)

// 水印
[17:23:13.400] warn: PipeGraph.hpp:268 resetGraph (0)VkInputLayer
[17:23:13.400] warn: PipeGraph.hpp:268 resetGraph (1)VkYUV2RGBALayer
[17:23:13.400] warn: PipeGraph.hpp:268 resetGraph (2)VkOutputLayer
[17:23:13.401] warn: PipeGraph.hpp:268 resetGraph (3)VkInputLayer
[17:23:13.401] warn: PipeGraph.hpp:268 resetGraph (4)VkBlendLayer
[17:23:13.401] info: PipeGraph.hpp:405 onValidNodes graph node from (0)VkInputLayer-0(rgba8) to (4)VkBlendLayer-0(rgba8)
[17:23:13.401] info: PipeGraph.hpp:405 onValidNodes graph node from (3)VkInputLayer-0(rgba8) to (4)VkBlendLayer-1(rgba8)
[17:23:13.401] info: PipeGraph.hpp:405 onValidNodes graph node from (4)VkBlendLayer-0(rgba8) to (2)VkOutputLayer-0(rgba8)
[17:23:13.402] info: VkInputLayer.cpp:56 onInitVkBuffer width:1920 height:1080 image type:rgba8
[17:23:13.406] info: VkInputLayer.cpp:56 onInitVkBuffer width:155 height:156 image type:rgba8
[17:23:13.410] info: VkVideoRender.cpp:146 onImageChange onImageChange width:1920 height:1080 format:rgba8
[17:23:13.411] info: Pipegraph reset success

// LUT
[17:23:41.549] warn: PipeGraph.hpp:268 resetGraph (0)VkInputLayer
[17:23:41.550] warn: PipeGraph.hpp:268 resetGraph (1)VkYUV2RGBALayer
[17:23:41.550] warn: PipeGraph.hpp:268 resetGraph (2)VkOutputLayer
[17:23:41.550] warn: PipeGraph.hpp:268 resetGraph (3)VkLookupLayer
[17:23:41.550] warn: PipeGraph.hpp:268 resetGraph (4)VkInputLayer
[17:23:41.550] info: PipeGraph.hpp:405 onValidNodes graph node from (0)VkInputLayer-0(rgba8) to (3)VkLookupLayer-0(rgba8)
[17:23:41.551] info: PipeGraph.hpp:405 onValidNodes graph node from (4)VkInputLayer-0(rgba8) to (3)VkLookupLayer-1(rgba8)
[17:23:41.551] info: PipeGraph.hpp:405 onValidNodes graph node from (3)VkLookupLayer-0(rgba8) to (2)VkOutputLayer-0(rgba8)
[17:23:41.551] info: VkInputLayer.cpp:56 onInitVkBuffer width:1920 height:1080 image type:rgba8
[17:23:41.554] info: VkInputLayer.cpp:56 onInitVkBuffer width:512 height:512 image type:rgba8
[17:23:41.560] info: VkVideoRender.cpp:146 onImageChange onImageChange width:1920 height:1080 format:rgba8
[17:23:41.561] info: Pipegraph reset success

// 水印与LUT启用
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (0)VkInputLayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (1)VkYUV2RGBALayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (2)VkOutputLayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (3)VkInputLayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (4)VkBlendLayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (5)VkLookupLayer
[11:46:20.731] warn: PipeGraph.hpp:268 resetGraph (6)VkInputLayer
[11:46:20.732] info: PipeGraph.hpp:405 onValidNodes graph node from (0)VkInputLayer-0(rgba8) to (4)VkBlendLayer-0(rgba8)
[11:46:20.732] info: PipeGraph.hpp:405 onValidNodes graph node from (3)VkInputLayer-0(rgba8) to (4)VkBlendLayer-1(rgba8)
[11:46:20.732] info: PipeGraph.hpp:405 onValidNodes graph node from (4)VkBlendLayer-0(rgba8) to (5)VkLookupLayer-0(rgba8)
[11:46:20.732] info: PipeGraph.hpp:405 onValidNodes graph node from (6)VkInputLayer-0(rgba8) to (5)VkLookupLayer-1(rgba8)
[11:46:20.732] info: PipeGraph.hpp:405 onValidNodes graph node from (5)VkLookupLayer-0(rgba8) to (2)VkOutputLayer-0(rgba8)
```

[Vulkan滤镜展示](https://zhuanlan.zhihu.com/p/388055520)，根据需要自己组合，也可扩展实现，如果选硬解，非硬解也只有一次CPU到GPU的交互，算是全GPU流程，性能也有保障。



