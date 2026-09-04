# 播放器 IOS

## VideoToolbox 硬解

VideoToolbox 是 IOS 的硬解接口。

注意的点。

1. opengl 在 ios12 就废弃了，我本身是准备做 opengl 渲染的，但是发现其在 xcode 打开的 opengl 全部标黄，所以就改用 Metal 渲染，相对 vulkan 来说，Metal 的 API 简单很多。
2. IOS 硬解需要的数据包不能是 Annexb 格式的,并且需要分割包，所以需要预先处理，我是在添加到原始数据队列时，先检查数据包是否是多个 Annexb 包，先做分割，然后把以'001'开头的改为'0001'，为后续在 IOS 硬解时方便直接替换前面四个字节。
3. IOS 硬解需要确定包编码数据包，如 SEI 非可解码码，放入硬解队列会引起问题，所以需要分析 Nalu 类型。

下面是前期包处理。

```C++
void AVTrack::pushPacket(const AvoxPacket &data,
                         std::function<bool()> vaildFunc) {
  // 如果播放器表明一个AvoxPacket需要自己做分割
  if (trackType == TrackType::video && mediaPlayer &&
      !mediaPlayer->getPacketWholeNalu()) {
    splitAnnexbNalu(data, spiltBufs);
  }
  if (spiltBufs.size() <= 0) {
    // 如果满了,阻塞等待,vaildFunc是当前线程如果停了，就不要堵塞了
    packetQueue.enqueueWait<AvoxPacket>(data, copyBuf, vaildFunc);
  } else {
    // 如果是视频，可能会分割成多个NALU
    for (auto &item : spiltBufs) {
      // 如果满了,阻塞等待,vaildFunc是当前线程如果停了，就不要堵塞了
      packetQueue.enqueueWait<AvoxPacket>(item, copyBuf, vaildFunc);
    }
  }
  ...
}
// 分割包
void splitAnnexbNalu(const AvoxPacket &data, std::vector<AvoxPacket> &nalus) {
  nalus.clear();
  const uint8_t *buffer = data.data.data;
  int32_t size = static_cast<int32_t>(data.data.size);
  int32_t pos = 0;

  while (pos < size) {
    int startCodeLen = 0;
    if (pos + 4 <= size && buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 &&
        buffer[pos + 2] == 0x00 && buffer[pos + 3] == 0x01) {
      startCodeLen = 4;
    } else if (pos + 3 <= size && buffer[pos] == 0x00 &&
               buffer[pos + 1] == 0x00 && buffer[pos + 2] == 0x01) {
      startCodeLen = 3;
    } else {
      ++pos;
      continue;
    }

    int32_t nalu_start = pos;
    pos += startCodeLen;

    int32_t next_start = pos;
    while (next_start < size) {
      if ((next_start + 4 <= size && buffer[next_start] == 0x00 &&
           buffer[next_start + 1] == 0x00 && buffer[next_start + 2] == 0x00 &&
           buffer[next_start + 3] == 0x01) ||
          (next_start + 3 <= size && buffer[next_start] == 0x00 &&
           buffer[next_start + 1] == 0x00 && buffer[next_start + 2] == 0x01)) {
        break;
      }
      ++next_start;
    }

    AvoxPacket nalu = {};
    nalu.frameType = data.frameType;
    nalu.prefixSize = startCodeLen;
    nalu.pts = data.pts;
    nalu.dts = data.dts;
    nalu.data.data = const_cast<uint8_t *>(buffer + nalu_start);
    nalu.data.size = next_start - nalu_start;
    // 指向外部数据，不管理内存
    nalu.data.bRef = 1;
    nalus.push_back(nalu);
    pos = next_start;
  }
}
// annexb三转四
void form(const AvoxPacket &packet) {
  frameType = packet.frameType;
  bConfig = bConfigType(packet);
  const uint8_t *pdata = packet.data.data;
  prefixSize = packet.prefixSize;
  // 如果pack是以annexb格式并且以'00 00 01'开头的,扩展'00 00 00
  // 01'4字节,方便后面统一处理
  // 判断前三字节是否为0x00 0x00 0x01
  int32_t bAnnexB3 = 0;
  if (pdata[0] == 0x00 && pdata[1] == 0x00 && pdata[2] == 0x01) {
    // 如果是,则扩展为4字节
    prefixSize = 4;
    bAnnexB3 = 1;
  }
  pts = packet.pts;
  dts = packet.dts;
  size = packet.data.size + bAnnexB3;
  // 只有在本身少于packet.data.size才可能去调整
  if (buff.size() < size) {
    buff.resize(size);
  }
  memcpy(buff.data() + bAnnexB3, packet.data.data, packet.data.size);
  if (bAnnexB3) {
    buff[0] = 0x00;
  }
}
// 将ANNEX-B格式的NALU数据包转为AVCC格式，非配置帧
bool annexb2AvccPacket(PacketBuf &packet) {
  // 检查是否为 4 字节的 ANNEX-B 头
  if (packet.buff[0] != 0x00 || packet.buff[1] != 0x00 ||
      packet.buff[2] != 0x00 || packet.buff[3] != 0x01) {
    return false;
  }
  // 计算 NALU 数据长度（去除 4 字节 ANNEX-B 头）
  size_t naluSize = packet.size - 4;
  // 将 NALU 长度以大端字节序写入前 4 个字节
  packet.buff[0] = static_cast<uint8_t>(naluSize >> 24);
  packet.buff[1] = static_cast<uint8_t>(naluSize >> 16);
  packet.buff[2] = static_cast<uint8_t>(naluSize >> 8);
  packet.buff[3] = static_cast<uint8_t>(naluSize);
  return true;
}
```

IOS 硬解的流程，和网上流程大致一样，只有 Metal 渲染部分有区别。

```C++
#define AVOX_IOS_VIDEOTOOLBOX_TIMEOUT_US 2000

class IOSVDecoder : public VideoDecoder {
public:
  IOSVDecoder();
  virtual ~IOSVDecoder();

private:
  VTDecompressionSessionRef decompressionSession = nullptr;
  CMVideoFormatDescriptionRef videoFormatDescription = nullptr;
  bool bMetalRender = false;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;

  H264NalUnit h264Unit = {};
  H265NalUnit h265Unit = {};

public:
  void close();
  void updateYuvFormat();

  // AVDecoder
public:
  // 初始化
  virtual bool onInit() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual bool decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;

  // VideoDecoder
public:
  virtual void onClose() override;
  virtual void onFrameRelease(bool bRender, const GpuFrame &frame) override;

private:
  static void decompressionOutputCallback(
      void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
      VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
      CMTime presentationTimeStamp, CMTime presentationDuration);
};
void regIOSVDecoder() {
  RegFunc regFunc = {"ios video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_IOS_H264_DECODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoDecoder* {
                             return new IOSVDecoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_IOS_H265_DECODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoDecoder* {
                             return new IOSVDecoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

IOSVDecoder::IOSVDecoder() {
  bMetalRender = true;
  codecTH = VCodecTh::iosVT;
}

IOSVDecoder::~IOSVDecoder() { close(); }

void IOSVDecoder::close() {
  if (decompressionSession) {
    VTDecompressionSessionInvalidate(decompressionSession);
    CFRelease(decompressionSession);
    decompressionSession = nullptr;
  }
  if (decompressionSession) {
    if (getIosDeviceSystemVersion() >= 11) {
      VTDecompressionSessionWaitForAsynchronousFrames(decompressionSession);
    }
    VTDecompressionSessionInvalidate(decompressionSession);
    CFRelease(decompressionSession);
    decompressionSession = nullptr;
  }
}

void IOSVDecoder::updateYuvFormat() {
  // 更新 YUV 格式信息
  if (videoFormatDescription) {
    CMVideoDimensions dimensions =
        CMVideoFormatDescriptionGetDimensions(videoFormatDescription);
    yuvFormat.width = dimensions.width;
    yuvFormat.height = dimensions.height;
    // 这里需要根据实际情况解析 YUV 格式
    // 示例代码，可能需要调整
    yuvFormat.type = YuvType::nv12;
  }
}

bool IOSVDecoder::onInit() {
  float version = getIosDeviceSystemVersion();
  // 不支持硬解
  if (version < 8.0) {
    LOGFLF(LogLevel::warn, "ios version :", version,
           " not support hardware decoder");
    return false;
  }
  if (version < 11.0 && codecDesc.vcodecId == VCodecId::h265) {
    LOGFLF(LogLevel::warn, "ios version :", version,
           " not support h265 decoder");
    return false;
  }
  return true;
}

DecodeResult IOSVDecoder::onPreDecoder() {
  const auto &packets = trackContext->getConfigPackets();
  // 不包含start code信息
  std::vector<uint8_t> vpsData;
  std::vector<uint8_t> spsData;
  std::vector<uint8_t> ppsData;
  OSStatus status = errSecSuccess;
  if (codecDesc.vcodecId == VCodecId::h264) {
    if (packets.size() < 2) {
      return DecodeResult::noConfig;
    }
    bool has_sps = false, has_pps = false;
    for (auto &packet : packets) {
      // 获取NAL单元数据(跳过起始码)
      const uint8_t *nalu = packet.buff.data() + packet.prefixSize;
      int nalu_size = packet.size - packet.prefixSize;
      uint8_t nalu_type = nalu[0] & 0x1F;
      if (nalu_type == (uint8_t)H264NAL::NAL_SPS) {
        spsData.resize(nalu_size);
        memcpy(spsData.data(), nalu, nalu_size);
        has_sps = true;
      } else if (nalu_type == (uint8_t)H264NAL::NAL_PPS) {
        ppsData.resize(nalu_size);
        memcpy(ppsData.data(), nalu, nalu_size);
        has_pps = true;
      }
    }
    if (!has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing SPS or PPS in H264 stream");
      return DecodeResult::noConfig;
    }
    const uint8_t *const parameterSetPointers[2] = {spsData.data(),
                                                    ppsData.data()};
    const size_t parameterSetSizes[2] = {spsData.size(), ppsData.size()};
    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault, 2, parameterSetPointers, parameterSetSizes, 4,
        &videoFormatDescription);

  } else {
    if (packets.size() < 3) {
      return DecodeResult::noConfig;
    }
    // H265需要单独解析VPS/SPS/PPS
    bool has_vps = false, has_sps = false, has_pps = false;
    for (auto &packet : packets) {
      // 跳过起始码前缀(假设prefixSize包含起始码长度)
      const uint8_t *nalu = packet.buff.data() + packet.prefixSize;
      int nalu_size = packet.size - packet.prefixSize;
      // H265的NAL类型在第一个字节的2-7位
      uint8_t nalu_type = (nalu[0] >> 1) & 0x3F;
      switch (nalu_type) {
      case 32: // VPS
        vpsData.resize(nalu_size);
        memcpy(vpsData.data(), nalu, nalu_size);
        has_vps = true;
        break;
      case 33: // SPS
        spsData.resize(nalu_size);
        memcpy(spsData.data(), nalu, nalu_size);
        has_sps = true;
        break;
      case 34: // PPS
        ppsData.resize(nalu_size);
        memcpy(ppsData.data(), nalu, nalu_size);
        has_pps = true;
        break;
      }
    }
    if (!has_vps || !has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing VPS, SPS, or PPS in H265 stream");
      return DecodeResult::noConfig;
    }
    const uint8_t *const parameterSetPointers[3] = {
        vpsData.data(), spsData.data(), ppsData.data()};
    const size_t parameterSetSizes[3] = {vpsData.size(), spsData.size(),
                                         ppsData.size()};
    status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
        kCFAllocatorDefault, 3, parameterSetPointers, parameterSetSizes, 4,
        nullptr, &videoFormatDescription);
  }
  if (status != errSecSuccess || !videoFormatDescription) {
    LOGFLF(LogLevel::warn,
           "open ios hard decoder open videoformatdescription failed");
    return DecodeResult::openFailed;
  }
  bool bParse = parseConfigs();
  if (!bParse) {
    LOGFLF(LogLevel::warn,
           "parse configs failed: decoder parameters may contain incorrect "
           "information");
  }
  // create VTDecompressionSession
  VTDecompressionOutputCallbackRecord callback = {};
  callback.decompressionOutputCallback =
      IOSVDecoder::decompressionOutputCallback;
  callback.decompressionOutputRefCon = this;
  // 硬解选项
  NSDictionary *attr = nullptr;
  if (bMetalRender) {
    attr = [NSDictionary
        dictionaryWithObjectsAndKeys:
            [NSNumber numberWithBool:NO],
            (id)kCVPixelBufferOpenGLESCompatibilityKey,
            [NSNumber
                numberWithInt:kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange],
            (id)kCVPixelBufferPixelFormatTypeKey, nil];
  } else {
    attr = [NSDictionary
        dictionaryWithObjectsAndKeys:
            [NSNumber numberWithBool:NO],
            (id)kCVPixelBufferOpenGLESCompatibilityKey,
            [NSNumber
                numberWithInt:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange],
            (id)kCVPixelBufferPixelFormatTypeKey, nil];
  }
  status = VTDecompressionSessionCreate(
      kCFAllocatorDefault, videoFormatDescription, nullptr,
      (__bridge CFDictionaryRef)attr, &callback, &decompressionSession);
  if (status != noErr) {
    LOGFLF(LogLevel::warn,
           "open ios hard decoder open decompressionSession failed");
    return DecodeResult::openFailed;
  }
  updateYuvFormat();
  LOGFLF(LogLevel::info, "open ios hard decoder sucess,width:", yuvFormat.width,
         " height:", yuvFormat.height);
  return DecodeResult::success;
}

bool IOSVDecoder::decode(const AvoxPacket & packet_) {
  if (!decompressionSession) {
    return false;
  }
  if (packet->prefixSize == 0) {
    packet->prefixSize = 4;
  }
  // 是否可解码数据
  bool bDecode = false;
  bool bKeyFrame = false;
  const uint8_t *nalu = packet->buff.data() + packet->prefixSize;
  uint8_t ualUnit = 0;
  if (codecDesc.vcodecId == VCodecId::h264) {
    ualUnit = nalu[0] & 0x1F;
    h264Unit.nal = (H264NAL)ualUnit;
    bDecode = h264Unit.decodeAble();
    bKeyFrame = h264Unit.keyFrame();
  } else {
    ualUnit = (nalu[0] >> 1) & 0x3F;
    h265Unit.nal = (H265NAL)ualUnit;
    bDecode = h265Unit.decodeAble();
    bKeyFrame = h265Unit.keyFrame();
  }
  // ios里不能解码的包不要输入,可能引起问题
  if (!bDecode) {
    return false;
  }
  if (bKeyFrame) {
    // LOGFLF(LogLevel::info, "key frame");
  }
  // IOS硬解不支持annb为3的包,转换成4头,但是这个前面一般会有转换,所以这里应该不会出现
  if (packet->prefixSize == 3) {
    log(LogLevel::warn, "ios hard decoder not support annb 3");
    return false;
  }
  // 如果是annb头4的包,需要把annb头4转化为avcc
  // 并且需要保证当前包里只有一个naul包,IO层需要保证有拆组合naul包的逻辑
  annexb2AvccPacket(*packet);
  CMBlockBufferRef videoBlock = nullptr;
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      nullptr, packet->buff.data(), packet->size, kCFAllocatorNull, nullptr, 0,
      packet->size, 0, &videoBlock);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "create with memory block error:", status);
    return false;
  }
  CMSampleBufferRef sampleBuffer = nullptr;
  const size_t sampleSizeArray[] = {packet->size};
  CMSampleTimingInfo timingInfo = {};
  int32_t timeScale = 90000;
  timingInfo.presentationTimeStamp =
      CMTimeMakeWithSeconds(packet->pts, timeScale);
  // timingInfo.duration = CMTimeMakeWithSeconds(packet->, timeScale);
  timingInfo.decodeTimeStamp = kCMTimeInvalid;
  status = CMSampleBufferCreate(kCFAllocatorDefault, videoBlock, true, nullptr,
                                nullptr, videoFormatDescription, 1, 1,
                                &timingInfo, 1, sampleSizeArray, &sampleBuffer);
  if (status != noErr) {
    CFRelease(videoBlock);
    LOGFLF(LogLevel::warn, "buffer create error:", status);
    return false;
  }

  VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
  VTDecodeInfoFlags flagOut = 0;
  status = VTDecompressionSessionDecodeFrame(decompressionSession, sampleBuffer,
                                             flags, nullptr, &flagOut);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "session decode frame error:", status);
  }
  CFRelease(sampleBuffer);
  CFRelease(videoBlock);
  return status == noErr;
}

void IOSVDecoder::flush() {
  if (decompressionSession) {
    // VTDecompressionSessionFlush(decompressionSession);
  }
}

void IOSVDecoder::onClose() { close(); }

void IOSVDecoder::onFrameRelease(bool bRender, const GpuFrame &frame) {
  CVImageBufferRef imageBuffer = static_cast<CVImageBufferRef>(frame.buffer);
  CFRelease(imageBuffer);
}

void IOSVDecoder::decompressionOutputCallback(
    void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
    VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
    CMTime presentationTimeStamp, CMTime presentationDuration) {
  int64_t pts = presentationTimeStamp.value / presentationTimeStamp.timescale;
  if (status != noErr || !imageBuffer) {
    LOGFLF(LogLevel::warn, "ios hard decoder decode failed,status:", status);
    // 12909,常见的错误,是否需要计数等特殊处理
    if (status == kVTVideoDecoderBadDataErr) {
    }
    return;
  }
  IOSVDecoder *decoder = static_cast<IOSVDecoder *>(decompressionOutputRefCon);
  if (decoder) {
    // 处理解码后的图像数据
    if (decoder->bMetalRender) {
      // 增加引用
      CFRetain(imageBuffer);
      // 使用 Metal 渲染
      GpuFrame frame = {};
      frame.pts = pts;
      frame.dts = frame.pts;
      frame.buffer = imageBuffer;     
      decoder->dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
    } else {
      YUVFrame frame = {};
      frame.pts = pts;
      frame.dts = frame.pts;
      frame.format = decoder->yuvFormat;
      // 锁定图像缓冲区的基地址以便访问数据
      CVPixelBufferLockBaseAddress(imageBuffer, 0);
      size_t planeCount = CVPixelBufferGetPlaneCount(imageBuffer);
      for (size_t i = 0; i < planeCount; ++i) {
        uint8_t *planeData = static_cast<uint8_t *>(
            CVPixelBufferGetBaseAddressOfPlane(imageBuffer, i));
        frame.data[i] = planeData;
        frame.stride[i] = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, i);
      }
      // 解锁图像缓冲区的基地址
      CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
      decoder->dispatch(&IVideoDecoderOb::onDecode, frame);
    }
  }
}
```

MetalKit 把 NV12 的纹理渲染成 RGBA 窗口。

```C++
class MetalGraph : public MetalContext {
public:
  MetalGraph();
  virtual ~MetalGraph();

protected:
  id<MTLRenderPipelineState> pipelineState = nil;
  CVMetalTextureCacheRef textureCache = nullptr;
  id<MTLTexture> yTexture = nil;
  id<MTLTexture> uvTexture = nil;
  id<MTLSamplerState> samplerState = nil;
  id<MTLRenderCommandEncoder> commandEncoder = nil;
  CAMetalLayer *metalLayer = nullptr;

public:
  void initSurface(CAMetalLayer *metalLayer);

private:
  void createPipelineState();
  void createTextureCache();
  void closePipelineState();
  void closeTextureCache();

public:
  void updateNV12ToMetalLayer(CVImageBufferRef imageBuffer);
};


// 顶点数据
const float vertices[] = {
    // 第一个三角形
    -1.0f, -1.0f, 0.0f, 0.0f, // 左下角
    1.0f, -1.0f, 1.0f, 0.0f,  // 右下角
    -1.0f, 1.0f, 0.0f, 1.0f,  // 左上角

    // 第二个三角形
    1.0f, -1.0f, 1.0f, 0.0f, // 右下角
    1.0f, 1.0f, 1.0f, 1.0f,  // 右上角
    -1.0f, 1.0f, 0.0f, 1.0f  // 左上角
};

// 单独处理 #include 指令
NSString *const nv12trgbPrefix =
    @"#include <metal_stdlib>\nusing namespace metal;\n";
NSString *const nv12trgbBody = AVOX_SHADER_STRING(
    struct VertexIn {
      float2 position [[attribute(0)]];
      float2 texCoord [[attribute(1)]];
    };

    struct VertexOut {
      float4 position [[position]];
      float2 texCoord;
    };

    vertex VertexOut vertexShader(const VertexIn in [[stage_in]]) {
      VertexOut out;
      out.position = float4(in.position, 0.0, 1.0);
      out.texCoord.x = in.texCoord.x;
      out.texCoord.y = 1.0f - in.texCoord.y;
      return out;
    }

    fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                   texture2d<float> yTexture [[texture(0)]],
                                   texture2d<float> uvTexture [[texture(1)]],
                                   sampler sampler [[sampler(0)]]) {
      // 采样 Y 分量
      float y = yTexture.sample(sampler, in.texCoord).r;
      float2 uv = uvTexture.sample(sampler, in.texCoord).rg;

      // 调整 YUV 分量到标准范围
      const float yOffset = 16.0 / 255.0;
      const float uvOffset = 128.0 / 255.0;
      float3 yuv = float3(y - yOffset, uv - uvOffset);

      // 使用 BT.601 标准的 YUV 转 RGB 矩阵
      float3x3 conversionMatrix = float3x3(1.164, 0.000, 1.596, 1.164, -0.392,
                                           -0.813, 1.164, 2.017, 0.000);
      float3 rgb = yuv * conversionMatrix;

      // 限制 RGB 分量在 [0, 1] 范围内
      rgb = clamp(rgb, 0.0, 1.0);
      return float4(rgb.r, rgb.g, rgb.b, 1.0);
      return float4(rgb, 1.0);
    });

NSString *const nv12trgb =
    [NSString stringWithFormat:@"%@%@", nv12trgbPrefix, nv12trgbBody];

MetalGraph::MetalGraph() { initContext(); }

MetalGraph::~MetalGraph() {
  closePipelineState();
  closeTextureCache();
}

void MetalGraph::initSurface(CAMetalLayer *metalLayer_) {
  metalLayer = metalLayer_;
  createPipelineState();
  createTextureCache();
}

void MetalGraph::createPipelineState() {
  // 创建渲染管线描述符
  MTLRenderPipelineDescriptor *pipelineDescriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  // 创建顶点描述符
  MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
  // 配置顶点属性 0: position
  vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[0].offset = 0;
  vertexDescriptor.attributes[0].bufferIndex = 0;
  // 配置顶点属性 1: texCoord
  vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[1].offset =
      2 * sizeof(float); // 偏移 2 个 float 的大小
  vertexDescriptor.attributes[1].bufferIndex = 0;
  // 配置顶点缓冲区布局
  vertexDescriptor.layouts[0].stride =
      4 *
      sizeof(float); // 每个顶点包含 4 个 float (2 个 position + 2 个 texCoord)
  vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  vertexDescriptor.layouts[0].stepRate = 1;

  // 将顶点描述符关联到渲染管线描述符
  pipelineDescriptor.vertexDescriptor = vertexDescriptor;

  NSError *libraryError = nil;
  id<MTLLibrary> library = [device newLibraryWithSource:nv12trgb
                                                options:nil
                                                  error:&libraryError];
  if (!library) {
    LOGFLF(LogLevel::warn, "failed to create library");
    return;
  }
  id<MTLFunction> vertexFunction =
      [library newFunctionWithName:@"vertexShader"];
  id<MTLFunction> fragmentFunction =
      [library newFunctionWithName:@"fragmentShader"];

  pipelineDescriptor.vertexFunction = vertexFunction;
  pipelineDescriptor.fragmentFunction = fragmentFunction;
  pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

  NSError *pipelineError = nil;
  pipelineState =
      [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                             error:&pipelineError];
  if (!pipelineState) {
    LOGFLF(LogLevel::warn, "failed to create pipeline state");
  }
  // 创建采样器状态
  MTLSamplerDescriptor *samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
  samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
  samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
  samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
  samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerDescriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
  samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];
}

void MetalGraph::createTextureCache() {
  CVReturn err = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                           nullptr, &textureCache);
  if (err != kCVReturnSuccess) {
    LOGFLF(LogLevel::warn, "failed to create texture cache");
  }
}

void MetalGraph::closePipelineState() {
  pipelineState = nil;
  yTexture = nil;
  uvTexture = nil;
}

void MetalGraph::closeTextureCache() {
  if (textureCache) {
    CFRelease(textureCache);
    textureCache = nullptr;
  }
}

void MetalGraph::updateNV12ToMetalLayer(CVImageBufferRef imageBuffer) {
  id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
  if (!imageBuffer || !textureCache || !drawable || !pipelineState) {
    LOGFLF(LogLevel::warn, "failed to create createpipelineState");
    return;
  }

  size_t width = CVPixelBufferGetWidth(imageBuffer);
  size_t height = CVPixelBufferGetHeight(imageBuffer);

  // 创建 Y 平面纹理
  CVMetalTextureRef yTextureRef = nullptr;
  CVReturn err = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, imageBuffer, nullptr,
      MTLPixelFormatR8Unorm, width, height, 0, &yTextureRef);
  if (err == kCVReturnSuccess) {
    yTexture = CVMetalTextureGetTexture(yTextureRef);
    CFRelease(yTextureRef);
  } else {
    LOGFLF(LogLevel::warn, "failed to create y texture");
    return;
  }
  // 创建 UV 平面纹理
  CVMetalTextureRef uvTextureRef = nullptr;
  err = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, imageBuffer, nullptr,
      MTLPixelFormatRG8Unorm, width / 2, height / 2, 1, &uvTextureRef);
  if (err == kCVReturnSuccess) {
    uvTexture = CVMetalTextureGetTexture(uvTextureRef);
    CFRelease(uvTextureRef);
  } else {
    LOGFLF(LogLevel::warn, "failed to create uv texture");
    return;
  }

  id<MTLCommandBuffer> commandBuffer = [getCommandQueue() commandBuffer];

  MTLRenderPassDescriptor *renderPassDescriptor =
      [MTLRenderPassDescriptor renderPassDescriptor];
  renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
  renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
  renderPassDescriptor.colorAttachments[0].clearColor =
      MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
  renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

  commandEncoder =
      [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
  [commandEncoder setRenderPipelineState:pipelineState];
  // 绑定采样器状态到索引 0 的采样器位置
  [commandEncoder setFragmentSamplerState:samplerState atIndex:0];
  // 设置顶点缓冲区
  [commandEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
  // 设置纹理
  [commandEncoder setFragmentTexture:yTexture atIndex:0];
  [commandEncoder setFragmentTexture:uvTexture atIndex:1];
  // 绘制
  [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                     vertexStart:0
                     vertexCount:6];

  [commandEncoder endEncoding];
  [commandBuffer presentDrawable:drawable];
  [commandBuffer commit];
}

class MetalRender : public VideoRender, public RunTask {
public:
  MetalRender();
  virtual ~MetalRender();

public:
  void start() override;
  void pause(bool pause) override;
  void flush() override;
  void close() override;

protected:
  virtual void onRunTask() override;

private:
  // 当窗口有效时，GPU纹理渲染，否则保持读取数据但是不渲染
  void readFrame();

public:
  virtual void onWinUpdate(IWindow *context);
};

void regIOSVRender() {
  RegFunc metalRenderReg = {
      "metal render init", []() {
        VRenderDesc renderDesc = {};
        renderDesc.name = "Metal Render";
        AvoxManager::Get().vRender.regInitFunc(
            RenderType::Metal, renderDesc,
            []() -> VideoRender * { return new MetalRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(metalRenderReg);
}

MetalRender::MetalRender() {}

MetalRender::~MetalRender() {}

void MetalRender::start() {
  taskName = "metal video render thread";
  addWindowOb();
  startTask();
}

void MetalRender::pause(bool pause) {
  if (pause) {
    pauseTask();
  } else {
    resumeTask();
  }
}

void MetalRender::flush() {
  // 刷新逻辑
}

void MetalRender::close() {
  stopTask();
  removeWindowOb();
}

void MetalRender::readFrame() {
  MetalWindow *metalWindow = dynamic_cast<MetalWindow *>(getWindow());
  auto frameAction = [&](const VideoFramePtr &frame) {
    // log(LogLevel::info, "onFrame pts:", frame->pts);
    if (frame->buffer->getCodecTh() == VCodecTh::iosVT) {
      // 检查窗口是否有效，无效则不渲染
      bool bValid = bWindowValid();
      // 引起AndVDecoder::onRender调用
      // bValid表明是否把MediaCodec解码队列的数据压入到OES纹理中，否则直接释放
      if (bValid) {
        // 解码后的OES纹理复制到Surface上的RGBA纹理上
        HwVideoBuffer *hwBuffer =
            dynamic_cast<HwVideoBuffer *>(frame->buffer.get());
        if (hwBuffer && metalWindow) {
          CVImageBufferRef imageBuffer =
              (CVImageBufferRef)hwBuffer->getHwBuffer();
          metalWindow->getGraph()->updateNV12ToMetalLayer(imageBuffer);
        }
      }
      frame->release(bValid);
    }
  };
  // 确定是否需要刷新画画
  if (trackContext->syncVideo()) {
    //  FrameQueue里的线程锁下执行
    bool bGet = trackContext->getFrameQueue().dequeueAction(frameAction);
    trackContext->onFrameResult(bGet);
  }
}

void MetalRender::onRunTask() {
  while (running()) {
    bool bYield = false;
    checkWindow();
    // 窗口无效时，直接读取数据不渲染
    if (!bWindowValid()) {
      readFrame();
    }
    sleepThread(bYield, 10);
    waitPause();
  }
}

void MetalRender::onWinUpdate(IWindow *context) {
  // 窗口渲染进程Tick
  readFrame();
}
```

有点奇怪，NV12 渲染线程与解码线程应该不在一起，也没设定同步代码，但是测试是无问题的。
