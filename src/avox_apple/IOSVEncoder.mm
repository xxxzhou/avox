#include "IOSVEncoder.hpp"
#include "IOSHelper.h"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox_vulkan/ios/VkIosImage.hpp"

namespace avox {

void regIOSVEncoder() {
  RegFunc regFunc = {"ios video encoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_IOS_H264_ENCODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoEncoder * {
                             return new IOSVEncoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_IOS_H265_ENCODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoEncoder * {
                             return new IOSVEncoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

IOSVEncoder::IOSVEncoder() { bMetalRender = true; }

IOSVEncoder::~IOSVEncoder() { onClose(); }

namespace {

// VTSessionSetProperty 会 retain, 传进去的那个引用由调用方自己放 —— 旧代码把
// CFNumberCreate 的结果直接当参数传, 每次开会话漏两个 CFNumber
void setSessionProperty(VTCompressionSessionRef session, CFStringRef key,
                        CFTypeRef value) {
  if (session && key && value) {
    VTSessionSetProperty(session, key, value);
  }
}

void setIntProperty(VTCompressionSessionRef session, CFStringRef key,
                    int32_t value) {
  CFNumberRef number =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
  if (!number) {
    return;
  }
  setSessionProperty(session, key, number);
  CFRelease(number);
}

void setFloatProperty(VTCompressionSessionRef session, CFStringRef key,
                      double value) {
  CFNumberRef number =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &value);
  if (!number) {
    return;
  }
  setSessionProperty(session, key, number);
  CFRelease(number);
}

void setBoolProperty(VTCompressionSessionRef session, CFStringRef key,
                     bool value) {
  setSessionProperty(session, key, value ? kCFBooleanTrue : kCFBooleanFalse);
}

// 码率上硬限(每 1 秒一个采样): cbr 模式下 VT 的压峰手段。
// 注意该属性在部分系统上不受支持 —— VTSessionSetProperty 会返回错误, 我们
// 忽略返回值, 退化成"只有平均码率", 与 vbr 同效(见 doc_record.md 平台支持注)
void setDataRateLimit(VTCompressionSessionRef session, int32_t bitsPerSecond) {
  int32_t seconds = 1;
  CFNumberRef bits =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bitsPerSecond);
  CFNumberRef secs =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &seconds);
  if (!bits || !secs) {
    if (bits) {
      CFRelease(bits);
    }
    if (secs) {
      CFRelease(secs);
    }
    return;
  }
  const void* values[] = {bits, secs};
  CFArrayRef limits =
      CFArrayCreate(kCFAllocatorDefault, values, 2, &kCFTypeArrayCallBacks);
  if (limits) {
    setSessionProperty(session, kVTCompressionPropertyKey_DataRateLimits,
                       limits);
    CFRelease(limits);
  }
  CFRelease(bits);
  CFRelease(secs);
}

// 属性键用字面量: PrioritizeEncodingSpeedOverQuality 这个常量在不同 SDK 版本
// 的可见性不一致(未声明会编译失败), 而 VT 的属性键字符串是稳定的; 不受支持的
// 系统上该设置会被忽略(不报错), 只在"速度档"时下发
CFStringRef kPrioSpeedKey = CFSTR("PrioritizeEncodingSpeedOverQuality");

}  // namespace

DecodeResult IOSVEncoder::onPreEncoder() {
  float version = getIosDeviceSystemVersion();
  // 检查iOS版本支持
  if (version < 8.0) {
    LOGFLF(LogLevel::warn, "ios version :", version,
           " not support hardware encoder");
    return DecodeResult::noSupport;
  }
  if (version < 11.0 && desc.codecId == VCodecId::h265) {
    LOGFLF(LogLevel::warn, "ios version :", version,
           " not support h265 encoder");
    return DecodeResult::noSupport;
  }
  OSStatus status = noErr;
  CMVideoCodecType codecType = kCMVideoCodecType_H264;
  // 根据编码器类型选择编码格式
  switch (desc.codecId) {
  case VCodecId::h264:
    codecType = kCMVideoCodecType_H264;
    break;
  case VCodecId::h265:
    codecType = kCMVideoCodecType_HEVC;
    break;
  default:
    LOGFLF(LogLevel::warn, "unsupported codec:", getVCodecName(desc.codecId));
    return DecodeResult::noSupport;
  }
  // 创建压缩会话
  status = VTCompressionSessionCreate(
      kCFAllocatorDefault, desc.desc.width, desc.desc.height, codecType,
      nullptr, nullptr, nullptr, &IOSVEncoder::compressionOutputCallback, this,
      &compressionSession);
  if (status != noErr || !compressionSession) {
    LOGFLF(LogLevel::warn, "failed to create compression session:", status);
    return DecodeResult::openFailed;
  }
  // 设置编码参数。RealTime = 延迟优先, 由用途档定(不是预设):
  //   record = 离线落盘: 关 RealTime 让 VT 走画质优化的多遍/前瞻路径, 开 B 帧
  //   stream = 实时推流: RealTime 开、关重排、码率当硬目标
  const bool bRec = encodeParam.usage == EncodeUsage::record;
  setBoolProperty(compressionSession, kVTCompressionPropertyKey_RealTime,
                  !bRec);
  // profile: 可移植三档 → VT 的 profile level; HEVC 三档都 Main(输入 8bit)。
  // 推流侧缺省取 compat(Baseline): 与 WebRTC SDP 的 profile-level-id 契约一致,
  // 编码器偷偷发 High 的 NAL 会让对端解不了
  if (desc.codecId == VCodecId::h264) {
    switch (getProfileLevel()) {
      case EncodeProfile::compat:
        setSessionProperty(compressionSession,
                           kVTCompressionPropertyKey_ProfileLevel,
                           kVTProfileLevel_H264_Baseline_AutoLevel);
        break;
      case EncodeProfile::main:
        setSessionProperty(compressionSession,
                           kVTCompressionPropertyKey_ProfileLevel,
                           kVTProfileLevel_H264_Main_AutoLevel);
        break;
      default:
        setSessionProperty(compressionSession,
                           kVTCompressionPropertyKey_ProfileLevel,
                           kVTProfileLevel_H264_High_AutoLevel);
        break;
    }
  } else {
    setSessionProperty(compressionSession, kVTCompressionPropertyKey_ProfileLevel,
                       kVTProfileLevel_HEVC_Main_AutoLevel);
  }
  // B 帧重排: 一律关。record 曾开(画质取向), 但 macOS 26 的 VT 对重排队列给
  // 的 decodeTimeStamp 是"输出槽位×帧距"而非真解码序 —— B 帧包 dts 落到 pts
  // 之后(如 pts=33/dts=100), mp4 封装按非单调拒收 EINVAL, 30fps 导出实际丢半
  // (1030 muxdbg/vtdbg 双端插桩定谳)。录制管线与 Windows MF 车道(无 B)对齐;
  // 要 B 帧画质得先在封装侧重导 dts, 另立工程
  setBoolProperty(compressionSession,
                  kVTCompressionPropertyKey_AllowFrameReordering, false);
  // 预设: VT 只有"是否把速度排在质量前面"这一个开关, 均衡档即不下发(取系统缺省)
  const EncodePreset preset = getPreset();
  if (preset != EncodePreset::inherit &&
      preset != EncodePreset::balanced) {
    setBoolProperty(compressionSession, kPrioSpeedKey,
                    preset == EncodePreset::speed);
  }
  // 码控模式: 质量优先只给 Quality(与 AverageBitRate 同给时 VT 以码率为准,
  // 质量档就废了; Quality 量纲 0.0~1.0 越大越清晰, 与统一档同向除以 100);
  // VBR/CBR 给平均码率, CBR 再叠一层每秒硬限尽力压峰
  const EncodeRateMode rcMode = getRateMode();
  if (rcMode == EncodeRateMode::quality) {
    setFloatProperty(compressionSession, kVTCompressionPropertyKey_Quality,
                     getQualityLevel() / 100.0);
  } else {
    setIntProperty(compressionSession, kVTCompressionPropertyKey_AverageBitRate,
                   getTargetBitrate());
    if (rcMode == EncodeRateMode::cbr) {
      setDataRateLimit(compressionSession, getTargetBitrate());
    }
  }
  // 设置帧率
  const int32_t fps = (int32_t)desc.desc.fps;
  setIntProperty(compressionSession, kVTCompressionPropertyKey_ExpectedFrameRate,
                 fps);
  // 设置关键帧间隔
  const int32_t keyFrameInterval = (int32_t)(gop * desc.desc.fps);
  setIntProperty(compressionSession,
                 kVTCompressionPropertyKey_MaxKeyFrameInterval,
                 keyFrameInterval);
  LOGFLF(LogLevel::info, "ios encoder open, usage:",
         bRec ? "record" : "stream", " rateMode:", (int32_t)rcMode,
         " bitrate:", getTargetBitrate(), " quality:", getQualityLevel(),
         " preset:", (int32_t)preset, " profile:", (int32_t)getProfileLevel(),
         " gop:", gop);
  // 准备编码会话
  status = VTCompressionSessionPrepareToEncodeFrames(compressionSession);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "failed to prepare compression session:", status);
    return DecodeResult::startFailed;
  }
  return DecodeResult::success;
}

void IOSVEncoder::flush() {
  if (compressionSession) {
    VTCompressionSessionCompleteFrames(compressionSession, kCMTimeInvalid);
  }
}

// 排空: 与 flush 同实现 —— CompleteFrames 本就是"吐完待出帧再返回"的 drain
// 语义。开 B 帧后会话内部会缓冲若干帧, 停流不排空就会丢尾帧
void IOSVEncoder::drain() { flush(); }

void IOSVEncoder::onClose() {
  if (compressionSession) {
    // 先排空: CompleteFrames 保证待出帧在返回前全部回调完(此时调用方还没放
    // sink, 尾帧能正常写出); 再落关闭闸丢迟到回调, 最后 Invalidate/Release
    VTCompressionSessionCompleteFrames(compressionSession, kCMTimeInvalid);
    bClosing.store(true);
    VTCompressionSessionInvalidate(compressionSession);
    CFRelease(compressionSession);
    compressionSession = nullptr;
  }
  if(pixelBuffer){
    CVPixelBufferRelease(pixelBuffer);
    pixelBuffer = nullptr;
  }
  preSurface = nullptr;
}

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
    CMTime presentationTime = CMTimeMake(frame.pts, 1000);
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
    CMTime presentationTime = CMTimeMake(frame.pts, 1000);
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

DecodeResult IOSVEncoder::encode(const YUVFrame &frame) {
  if (!compressionSession) {
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 创建CVPixelBuffer用于YUV数据编码
  CVPixelBufferRef pixelBuffer = nullptr;
  NSDictionary *pixelBufferAttributes = @{
    (NSString *)kCVPixelBufferPixelFormatTypeKey :
        @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
    (NSString *)kCVPixelBufferWidthKey : @(frame.format.width),
    (NSString *)kCVPixelBufferHeightKey : @(frame.format.height),
    (NSString *)kCVPixelBufferIOSurfacePropertiesKey : @{}
  };
  CVReturn result = CVPixelBufferCreate(
      kCFAllocatorDefault, frame.format.width, frame.format.height,
      kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
      (__bridge CFDictionaryRef)pixelBufferAttributes, &pixelBuffer);
  if (result != kCVReturnSuccess || !pixelBuffer) {
    LOGFLF(LogLevel::warn, "failed to create pixel buffer:", result);
    return DecodeResult::dataError;
  }
  // 锁定像素缓冲区并复制YUV数据
  CVPixelBufferLockBaseAddress(pixelBuffer, 0);
  uint8_t *yPlane =
      (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
  uint8_t *uvPlane =
      (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
  size_t yPlaneBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
  size_t uvPlaneBytesPerRow =
      CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
  // 复制Y平面数据
  if (frame.data[0] && yPlane) {
    if (yPlaneBytesPerRow == frame.stride[0]) {
      memcpy(yPlane, frame.data[0], frame.stride[0] * frame.format.height);
    } else {
      for (int i = 0; i < frame.format.height; i++) {
        memcpy(yPlane + i * yPlaneBytesPerRow,
               frame.data[0] + i * frame.stride[0], frame.format.width);
      }
    }
  }
  // 复制UV平面数据
  if (frame.data[1] && uvPlane) {
    if (uvPlaneBytesPerRow == frame.stride[1]) {
      memcpy(uvPlane, frame.data[1], frame.stride[1] * frame.format.height / 2);
    } else {
      for (int i = 0; i < frame.format.height / 2; i++) {
        memcpy(uvPlane + i * uvPlaneBytesPerRow,
               frame.data[1] + i * frame.stride[1], frame.format.width);
      }
    }
  }
  CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
  // 编码帧
  CMTime presentationTime = CMTimeMake(frame.pts, 1000);
  VTEncodeInfoFlags infoFlags = 0;
  OSStatus status = VTCompressionSessionEncodeFrame(
      compressionSession, pixelBuffer, presentationTime, kCMTimeInvalid,
      nullptr, nullptr, &infoFlags);
  CVPixelBufferRelease(pixelBuffer);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "failed to encode frame:", status);
    return DecodeResult::dataError;
  }
  return DecodeResult::success;
}

void IOSVEncoder::sendConfig(CMFormatDescriptionRef formatDesc, int64_t pts) {
  VCodecId codec = desc.codecId;
  std::vector<uint8_t> vpsData;
  std::vector<uint8_t> spsData;
  std::vector<uint8_t> ppsData;
  // 1. 提取参数集（VPS/SPS/PPS）
  const uint8_t *parameterSet = nullptr;
  size_t parameterSetSize = 0;
  size_t parameterSetCount = 0;
  // 注意IOS硬解出来的是avcc/hvcc头格式,配置帧使用同样格式
  if (codec == VCodecId::h264) {
    // H.264：提取 SPS 和 PPS
    CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        formatDesc, 0, &parameterSet, &parameterSetSize, &parameterSetCount,
        nullptr);
    if (parameterSet && parameterSetSize > 0) {
      spsData.resize(parameterSetSize + 4); // 预留 4 字节长度头AVCC
      *reinterpret_cast<uint32_t *>(spsData.data()) =
          htonl(parameterSetSize); // 大端序写入长度
      memcpy(spsData.data() + 4, parameterSet, parameterSetSize);
    }

    CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        formatDesc, 1, &parameterSet, &parameterSetSize, nullptr, nullptr);
    if (parameterSet && parameterSetSize > 0) {
      ppsData.resize(parameterSetSize + 4); // 预留 4 字节长度头
      *reinterpret_cast<uint32_t *>(ppsData.data()) =
          htonl(parameterSetSize); // 大端序写入长度
      memcpy(ppsData.data() + 4, parameterSet, parameterSetSize);
    }
  } else if (codec == VCodecId::h265) {
    // H.265：提取 VPS、SPS 和 PPS
    CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
        formatDesc, 0, &parameterSet, &parameterSetSize, &parameterSetCount,
        nullptr);
    if (parameterSet && parameterSetSize > 0) {
      vpsData.resize(parameterSetSize + 4);
      *reinterpret_cast<uint32_t *>(vpsData.data()) =
          htonl(parameterSetSize); // 大端序写入长度
      memcpy(vpsData.data() + 4, parameterSet, parameterSetSize);
    }
    CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
        formatDesc, 1, &parameterSet, &parameterSetSize, nullptr, nullptr);
    if (parameterSet && parameterSetSize > 0) {
      spsData.resize(parameterSetSize + 4);
      *reinterpret_cast<uint32_t *>(spsData.data()) =
          htonl(parameterSetSize); // 大端序写入长度
      memcpy(spsData.data() + 4, parameterSet, parameterSetSize);
    }
    CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
        formatDesc, 2, &parameterSet, &parameterSetSize, nullptr, nullptr);
    if (parameterSet && parameterSetSize > 0) {
      ppsData.resize(parameterSetSize + 4);
      *reinterpret_cast<uint32_t *>(ppsData.data()) =
          htonl(parameterSetSize); // 大端序写入长度
      memcpy(ppsData.data() + 4, parameterSet, parameterSetSize);
    }
  }
  // 顺序 vps->sps->pps
  // 2. 按顺序发送参数集
  auto sendParameterSet = [this, pts](std::vector<uint8_t> &data) {
    if (!data.empty()) {
      AvoxPacket packet = {};
      packet.packtype = (int32_t)PackType::vconfig;
      packet.data.data = data.data();
      packet.data.size = data.size();
      packet.data.bRef = false;
      packet.prefixSize = 4;
      packet.pts = pts; // 使用传入的 pts
      packet.dts = pts;
      dispatch(&IEncoderOb::onPacket, packet);
    }
  };
  if (codec == VCodecId::h264) {
    // H.264：发送 SPS → PPS
    sendParameterSet(spsData);
    sendParameterSet(ppsData);
  } else if (codec == VCodecId::h265) {
    // H.265：发送 VPS → SPS → PPS
    sendParameterSet(vpsData);
    sendParameterSet(spsData);
    sendParameterSet(ppsData);
  }
}

void IOSVEncoder::compressionOutputCallback(void *outputCallbackRefCon,
                                            void *sourceFrameRefCon,
                                            OSStatus status,
                                            VTEncodeInfoFlags infoFlags,
                                            CMSampleBufferRef sampleBuffer) {
  if (status != noErr || !sampleBuffer) {
    LOGFLF(LogLevel::warn, "ios hard encoder encode failed,status:", status);
    return;
  }
  IOSVEncoder *encoder = static_cast<IOSVEncoder *>(outputCallbackRefCon);
  if (!encoder)
    return;
  // 会话已开始关闭: 该帧属排空后的迟到回调, 丢弃(防打到已释放的下游)
  if (encoder->bClosing.load())
    return;
  // 获取编码数据
  CMBlockBufferRef dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
  if (!dataBuffer)
    return;
  size_t length = 0;
  char *dataPointer = nullptr;
  OSStatus dataStatus = CMBlockBufferGetDataPointer(dataBuffer, 0, nullptr,
                                                    &length, &dataPointer);
  if (dataStatus == noErr && dataPointer && length > 0) {
    // 创建AvoxPacket
    AvoxPacket packet = {};
    packet.packtype = (int32_t)PackType::video;
    packet.index = 0;
    packet.data.data = (uint8_t *)dataPointer;
    packet.data.size = length;
    packet.data.bRef = true;
    packet.prefixSize = 4;
    // 获取时间戳: 换回毫秒。曾用 value/timescale 整除, VT 会话 timescale 下
    // 亚秒全被量化成 0 → 全片 pts 恒 0, 下游 IOMuxer 按「同 dts=同帧」AU 合并
    // 把整片粘成一个 sample(产物秒完播+无声, 10/10 定谳)
    CMTime presentationTime =
        CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    packet.pts = (int64_t)llround(CMTimeGetSeconds(presentationTime) * 1000.0);
    // 开 B 帧重排后 pts 与 dts 不再相等: dts 必须取 VT 给解码时间, 否则容器里
    // 两者全等, 重排流会被下游当无重排处理(时间戳错乱)
    CMTime decodeTime = CMSampleBufferGetDecodeTimeStamp(sampleBuffer);
    if (CMTIME_IS_VALID(decodeTime)) {
      packet.dts = (int64_t)llround(CMTimeGetSeconds(decodeTime) * 1000.0);
    } else {
      packet.dts = packet.pts;
    }
    // 判断帧类型
    if (infoFlags & kVTEncodeInfo_FrameDropped) {
      // 丢弃的帧
      return;
    }
    // 判断是否为 I 帧
    CFArrayRef attachments =
        CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments) {
      CFDictionaryRef dict =
          (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
      bool bKeyFrame =
          !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
      packet.frameType = bKeyFrame ? 1 : 0; // 1: I 帧，0: 非 I 帧
      // 在I帧前,获得配置帧
      if (bKeyFrame) {
        // 检查是否为配置帧（VPS/SPS/PPS）
        CMFormatDescriptionRef formatDesc =
            CMSampleBufferGetFormatDescription(sampleBuffer);
        if (formatDesc) {
          encoder->sendConfig(formatDesc,packet.pts);
        }
      }
    }
    encoder->dispatch(&IEncoderOb::onPacket, packet);
  }
}

}
