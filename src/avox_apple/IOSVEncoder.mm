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
  // 设置编码参数
  VTSessionSetProperty(compressionSession, kVTCompressionPropertyKey_RealTime,
                       kCFBooleanTrue);
  VTSessionSetProperty(
      compressionSession, kVTCompressionPropertyKey_ProfileLevel,
      desc.codecId == VCodecId::h264 ? kVTProfileLevel_H264_Baseline_AutoLevel
                                     : kVTProfileLevel_HEVC_Main_AutoLevel);
  VTSessionSetProperty(compressionSession,
                       kVTCompressionPropertyKey_AllowFrameReordering,
                       kCFBooleanFalse);
  VTSessionSetProperty(
      compressionSession, kVTCompressionPropertyKey_AverageBitRate,
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bitrate));
  // 设置帧率
  int32_t fps = desc.desc.fps;
  VTSessionSetProperty(
      compressionSession, kVTCompressionPropertyKey_ExpectedFrameRate,
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &fps));
  // 设置关键帧间隔
  int32_t keyFrameInterval = gop * desc.desc.fps;
  VTSessionSetProperty(
      compressionSession, kVTCompressionPropertyKey_MaxKeyFrameInterval,
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &keyFrameInterval));
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

void IOSVEncoder::onClose() {
  if (compressionSession) {
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
  CMTime presentationTime = CMTimeMake(frame.pts, 1000000);
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
    // 获取时间戳
    CMTime presentationTime =
        CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    packet.pts = presentationTime.value / presentationTime.timescale;
    packet.dts = packet.pts;
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
