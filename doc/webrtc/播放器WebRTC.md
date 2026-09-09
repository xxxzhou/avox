# 播放器WebRTC多平台原生窗口渲染

WebRTC是一个支持P2P的实时通信协议，它的优势低延迟,复杂网络的流畅播放，可以不搭建服务器，直接通过浏览器实现视频通话。

想在各平台原生窗口中播放WebRTC视频，在前面播放器已实现功能中，从解协议，解码，图像处理与渲染，最后到原生窗口显示都有实现，最开始想的方案是直接拉WebRTC协议的原始包，然后同zlmediakit/ffmpeg拉流后一样处理，实现对当前播放器最大限度复用，如metaRTC可实现类似功能，但是这样会丢弃WebRTC相关优势如复杂网络与低延迟能最大限度流畅播放，因为这种方案没有用到WebRTC里JitterBuffer，延迟探测，丢包检测等功能。

因此确定新方案，只接管WebRTC已解码出的音视频数据，然后使用原播放器的图像处理管线与渲染到各原生窗口，这样最大限度利用WebRTC在复杂网络和底延迟特性，又能很好复用现播放器已实现及待扩展功能。

本文记录了如何在原播放器的基础上，添加WebRTC支持，下面是效果。

![WebRTC支持](../../assets/images/media/webrtc.png)

## WebRTC编译

主要参考[WebRTC全平台编译指南](https://pixpark.net/c706dac8.html)

最新稳定分支是M138,记录下遇到的问题。

* webrtc现M138分支不带视频编码/解码工厂build的API，可以直接在根目录下的BUILD.gn添加依赖api:enable_media_with_defaults，让WebRTC包含builtin_video_decoder_factory等模块。
* webrtc使用静态链接，对应MSVC的运行库是MT/MTd，为了适配，本身要去掉MD/MDd。
* 引用ffmpeg/openssl,这二者本身在当前项目都有引用，因此会有重复定义的问题，加上/FORCE:MULTIPLE可以避免这个问题，但是不同版本openssl,会导致webrtc在生成ssl上下文会crash，现在是选择是在链接zlmediakit时，关闭ENABLE_OPENSSL时，避免引入openssl,这样webrtc会用自身所带的openssl.这个问题很难查，还是注意到编译时提示已忽略第二个定义，想到启用MULTIPLE后，每次运行都到创建ssl上下文时crash时，才想到是webrtc有可能是链接到zlmediakit里使用openssl导致的。
* webrtc下，默认debug/release其D_ITERATOR_DEBUG_LEVEL=0，而正常debug下，其D_ITERATOR_DEBUG_LEVEL=2，所以is_debug=true里，需要打开enable_iterator_debugging=true，会让D_ITERATOR_DEBUG_LEVEL=2,但是这个选项打开后，m138又编译不过，把webrtc源目录下的src/third_party/protobuf/src/google/protobuf/port_def.inc文件,PROTOBUF_CONSTINIT constinit改为PROTOBUF_CONSTINIT，编译通过。
* MULTIPLE不应开启，事实证明，avox_webrtc/avox_ffmpeg里ffmpeg都用ffmepg7.1,但是分别链接不同函数，用MULTIPLE强制avox_webrtc使用avox_ffmepg里的ffmpeg实现，实测可正常编译，运行时也不crash,但是解码数据不对，删除CMake里的MULTIPLE编译选项，故现在AVOX_ENABLE_WEBRTC后，会自动关闭AVOX_ENABLE_FFMPEG选项。至于为什么不都用动态链接ffmpeg避免这个问题，主要是考虑到IOS上动态链接会比较麻烦，先尝试别的解决方案。

## 原生窗口播放WebRTC流程

主要参考[webrtc 点对点会话建立过程分析](https://blog.csdn.net/zhuiyuanqingya/article/details/84108763)

简单来说，拉WebRTC的流播放如下：

1. 初始化WebRTC环境，包括创建PeerConnectionFactory,创建PeerConnection,PeerConnection创建本地CreateOffer，在其回调中SetLocalDescription，因为WebRTC没有指定SDP交互具体方式，在这使用的ZLMediakit做服务器，把本地的SDP发送给ZLMediakit,ZLMediakit服务会返回AnswerSdp,PeerConnection设置SetRemoteDescription完成，这样本地与服务器就各自清楚对方的媒体支持情况，WebRTC内部根据双方情况，选择都支持的协议如H264，各方再选择相应的编码器或解码器。
2. 在OnAddTrack回调中，添加解码后的Track回调处理。
3. 在Track回调OnFrame中，接管YUV数据并对接原播放器方案中图像处理管理，共用原生窗口渲染方案。

## WebRTC服务

方便测试，一般在本地会搭建一个WebRTC服务，如ZLMediakit，这里以ZLMediakit为例，主要流程如下。

* 安装OpenSSL,直接到官网下载安装程序就行。
* 再安装RTSP,[windows电脑安装libsrtps](https://avmedia.0voice.com/?id=67801) 实测，vcpkg安装的设置openssl选项有问题,其WebRTC服务会返回unspecified failure (srtp_err_status_fail)，需要编译srtp时带--enable-openssl。直接用源码编译吧，也不麻烦，打开BUILD_SHARED_LIBS/ENABLE_OPENSSL选项,关闭ERR_WARNINGS_AS_ERRORS/LIBSRTP_TEST_APPS。
* 把服务器的mediaserve与当前使用的zlmediakit分开，服务器的需要webrtc/srtp相关，会引用openssl,与本项目静态链接webrtc使用BoringSSL加密重复会导致问题。
* 需要在mediaserve运行目录下，找到配置文件的rtsp节点，设置directProxy=0, 不然WebRTC客户端可能播放不了。

## 针对WebRTC引进的代码

最开始我是准备单独写个RTCPlayer,但是考虑到原MediaPlayer已经实现的功能，如在Windows平台，可以使用Dx11/Dx12/Vulkan原生窗口渲染，而在android平台，可以使用EGL/Vulkan渲染，而在IOS下，可以使用Metal/Vulkan渲染[播放器多平台Vulkan集成](https://zhuanlan.zhihu.com/p/1925515227908268800)，同样所有平台的画面都可使用Vulkan的图像处理管线[Vulkan移植GPUImage](https://zhuanlan.zhihu.com/p/388055520),以及Track与渲染窗口对接的流程几乎都一样，原播放器还有些细锁的功能如埋点序列化/反序列化，状态与命令队列等功能都有用，只有一个解码是用不上的，因此还是如zlmediakit/ffmpeg解网络协议的基类IOParseOb一样，主要在此基类增加直接返回解码数据的接口供WebRTC使用，以及在MediaPlayer添加IO源格式，表明是否不需要解码，针对不需要解码的部分添加相关处理。

WebRTC项目特别大，故不在项目本身集成，又因为WebRTC头文件不确定引用关系，不太方便直接把头文件复制出来，故需要在项目指定对应源目录，编译目录，由目录去选择,如下是本项目引用WebRTC的cmake代码：
 
``` cmake
# 需要指定WebRTC源码位置，从源码位置编译出webrtc库。
# 头文件与库都直接根据webrtc源码目录来设定，
# 不想把webrtc源码的头文件复制到当前项目中，因为太多文件了，其lib也很大。
if(WIN32)
    # 根据需要自己改动
    set(WEBRTC_SOURCE_DIR "D:/Work/webrtc/src")   
    if(AVOX_DEBUG)       
        set(WEBRTC_BUILD_DIR "D:/Work/webrtc/build/windows/debug")
    else()      
        set(WEBRTC_BUILD_DIR "D:/Work/webrtc/build/windows/release")
    endif()
endif()

message(STATUS "WEBRTC_BUILD_DIR:${WEBRTC_BUILD_DIR}")
# 查找WEBRTC的头文件目录
find_path(WEBRTC_INCLUDE_DIR
    NAMES api/peer_connection_interface.h
    PATHS ${WEBRTC_SOURCE_DIR}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH)
set(WEBRTC_INCLUDE_DIRS ${WEBRTC_INCLUDE_DIR})

# 设置WEBRTC库文件所在目录
set(WEBRTC_LIB_DIR ${WEBRTC_BUILD_DIR})

# 查找WEBRTC库
if(WIN32)
    find_library(WEBRTC_LIBRARY
        NAMES webrtc
        PATHS ${WEBRTC_LIB_DIR}/obj
        NO_DEFAULT_PATH)
elseif(APPLE OR ANDROID)
    find_library(WEBRTC_LIBRARY
        NAMES libwebrtc.a
        PATHS ${WEBRTC_LIB_DIR}
        NO_DEFAULT_PATH)
endif()

set(WEBRTC_LIBRARIES ${WEBRTC_LIBRARY})

message(STATUS "WEBRTC_INCLUDE_DIRS: ${WEBRTC_INCLUDE_DIRS}")
message(STATUS "WEBRTC_LIBRARIES: ${WEBRTC_LIBRARIES}")

# WEBRTC_FOUND变量
include(FindPackageHandleStandardArgs)

# 需要注意WEBRTC和文件FindWEBRTC.cmake要一致，大小写一致
find_package_handle_standard_args(WebRTC DEFAULT_MSG WEBRTC_LIBRARIES WEBRTC_INCLUDE_DIRS)
```

接管WebRTC代码到原播放器中。M138 对 H265 只是协议层支持（webrtc 本体没有 H265 解码器），avox 的做法是 H265 解码不依赖 webrtc 自带实现：`RtcVideoDecoder::Configure` 按 codec 从 avox 解码器注册表（`AvoxManager::vDecoders`）取内置解码器——平台硬解优先（`getDefaultDecoderName(codecId, true)`），FFmpeg 软解兜底，webrtc 侧只做 RTP 解包与码流解析（`H265BitstreamParser`）。即 H265 over WebRTC 的解码能力与普通播放器共用同一条内置解码链，与 webrtc 是否内置 H265 无关。

``` C++
void RtcParse::OnFrame(const webrtc::VideoFrame &frame) {
  int64_t ms = frame.render_time_ms();
  Timespan nowTime = {ms * 10000};
  auto frameBuffer = frame.video_frame_buffer();
  // 非硬解
  if (frameBuffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
        frameBuffer->ToI420();
    if (videoTracks.size() == 0) {
      VTrackDesc videoDesc = {};
      videoDesc.desc.width = i420_buffer->width();
      videoDesc.desc.height = i420_buffer->height();
      videoDesc.desc.type = YuvType::yuv420P;
      videoTracks.push_back(videoDesc);
      // if (!bOpenOb && audioTracks.size() > 0) {
      dispatch(&IAVSourceOb::onOpen);
      bOpenOb = true;
      //}
    }
    YUVFrame yuvFrame = {};
    yuvFrame.pts = frame.render_time_ms();
    yuvFrame.format.width = i420_buffer->width();
    yuvFrame.format.height = i420_buffer->height();
    yuvFrame.format.type = YuvType::yuv420P;
    yuvFrame.data[0] = const_cast<uint8_t *>(i420_buffer->DataY());
    yuvFrame.data[1] = const_cast<uint8_t *>(i420_buffer->DataU());
    yuvFrame.data[2] = const_cast<uint8_t *>(i420_buffer->DataV());
    yuvFrame.stride[0] = i420_buffer->StrideY();
    yuvFrame.stride[1] = i420_buffer->StrideU();
    yuvFrame.stride[2] = i420_buffer->StrideV();
    dispatch(&IAVSourceOb::onVideoFrame, yuvFrame);
  } else {
    // 硬解
#if __APPLE__
    RTCCVPixelBuffer *pixelBuffer =
        (RTCCVPixelBuffer *)NativeToObjCVideoFrameBuffer(
            frame.video_frame_buffer());
#endif

#if __ANDROID__
    auto buffer = frame.video_frame_buffer();
    // webrtc::AndroidH264H265VideoFrameBuffer *androidBuffer =
    //     reinterpret_cast<webrtc::AndroidH264H265VideoFrameBuffer *>(buffer);
#endif
  }
  // log(LogLevel::info, "webrtc onframe video:", nowTime);
}
```

原MediaPlayer相应的改动。

``` C++
// IO源类型，如需要解码，WebRTC不需要解码，YUV数据直出
#define AVOX_MAP_IO_SOURCE_TYPE(XX)    \
  XX(none, 0, "none")                 \
  XX(decode, 1, "decode")             \
  XX(webRtc, 2, "WebRTC")             \
  XX(device, 3, "Device")

enum class IoSourceType {
#define XX(name, value, str) name = value,
  AVOX_MAP_IO_SOURCE_TYPE(XX)
#undef XX
};

// IAVSourceOb增加解码后数据回调
class IAVSourceOb {
public:
  IAVSourceOb() = default;
  virtual ~IAVSourceOb() = default;

public:
  // track解析完成(类似SDP,音频/视频元数据)
  // 在这后才能调用IOParse里的gettrack才能获取正确信息
  virtual void onOpen() = 0;
  virtual void onClose() {}
  virtual void onComplete() {};
  virtual void onError(IoError error, const char *msg) = 0;
  virtual void onReopen() {}
  // 网络包解封装后的数据，如H264是Annexb/AVC等格式，音频是acc等格式
  virtual void onPacket(const AvoxPacket &frame, int32_t streamId = 0) = 0;
  // IO源不为decode时，直接返回视频帧
  virtual void onVideoFrame(const YUVFrame &frame) {};
  // GPU视频帧
  virtual void onGpuFrame(const GpuFrame &frame) {};
  // 音频帧
  virtual void onAudioFrame(const AvoxAFrame &frame) {};
};

class RtcParse : public RawSource,
                 public webrtc::PeerConnectionObserver,
                 public webrtc::RtpReceiverObserverInterface,
                 public webrtc::VideoSinkInterface<webrtc::VideoFrame>,
                 public webrtc::AudioTrackSinkInterface {
RtcParse() {
    sourceType = IoSourceType::webRtc;
}
}

void VideoTrack::start() {
  if (!needDecoder()) {
    // 直接打开渲染对象
    onVideoDesc();
    return;
  }
  // 打开解码对象
  ....
}
```
