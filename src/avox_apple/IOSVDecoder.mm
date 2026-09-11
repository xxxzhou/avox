#include "IOSVDecoder.hpp"
#include "IOSHelper.h"
#include "avox/codec/H26XHelper.hpp"
#include <TargetConditionals.h>
#include <iostream>

namespace avox {

void regIOSVDecoder() {
  RegFunc regFunc = {"ios video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_IOS_H264_DECODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoDecoder * {
                             return new IOSVDecoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_IOS_H265_DECODER;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoDecoder * {
                             return new IOSVDecoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

IOSVDecoder::IOSVDecoder() {
  bMetalRender = true;
  bMustVcc = true;
  codecTH = VCodecTh::iosVT;
}

IOSVDecoder::~IOSVDecoder() { onClose(); }

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

bool IOSVDecoder::onVaild() {
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
  const auto &packets = configPackets;
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
  //  bool bParse = parseConfigs();
  //  if (!bParse) {
  //    LOGFLF(LogLevel::warn,"parse configs failed");
  //  }
  // create VTDecompressionSession
  VTDecompressionOutputCallbackRecord callback = {};
  callback.decompressionOutputCallback =
      IOSVDecoder::decompressionOutputCallback;
  callback.decompressionOutputRefCon = this;
  // 硬解选项
  // NV12 kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
  // YUV420P  kCVPixelFormatType_420YpCbCr8PlanarVideoRange
  // 不带 kCVPixelBufferOpenGLESCompatibilityKey: 本解码器恒 Metal 渲染用不到
  // GLES 兼容, 且 iOS 26 已移除 OpenGL ES, 带 GLES 兼容键建会话会挂在废弃
  // GL 路径上 (真机实测视频解码线程停在会话创建, 包队列积压零帧)
  NSDictionary *attr = [NSDictionary
      dictionaryWithObjectsAndKeys:
          [NSNumber numberWithInt:kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange],
          (id)kCVPixelBufferPixelFormatTypeKey, nil];
  LOGFLF(LogLevel::info, "creating ios vt decompression session");
  status = VTDecompressionSessionCreate(
      kCFAllocatorDefault, videoFormatDescription, nullptr,
      (__bridge CFDictionaryRef)attr, &callback, &decompressionSession);
  LOGFLF(LogLevel::info, "ios vt decompression session created, status:",
         (int)status);
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

DecodeResult IOSVDecoder::decode(const AvoxPacket &packet_) {
  if ((codecDesc.vcodecId == VCodecId::h264 && configPackets.size() < 2) ||
      (codecDesc.vcodecId == VCodecId::h265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  if (!decompressionSession) {
    return onPreDecoder();
  }
  AvoxPacket packet = packet_;
//    AvoxData logData = {packet.data.data,std::min(16,packet.data.size),true};
//    LOGFLF(LogLevel::info,"data:",logData," size:",packet.data.size);
  uint8_t ualUnit = getNalUnit(codecDesc.vcodecId, packet);
  // 是否可解码数据
  bool bDecode = naluDataFrame(codecDesc.vcodecId, ualUnit);
  bool bKeyFrame = naluKeyFrame(codecDesc.vcodecId, ualUnit);
  // ios里不能解码的包不要输入,可能引起问题
  if (!bDecode) {
    return DecodeResult::dataError;
  }
  if (bKeyFrame) {
    // LOGFLF(LogLevel::info, "key frame");
  }
  // IOS硬解不支持annb为3的包,转换成4头
  // 但是这个前面一般会有转换,所以这里应该不会出现
  if (packet.prefixSize == 3) {
    log(LogLevel::warn, "ios hard decoder not support annb 3");
    return DecodeResult::dataError;
  }
  CMBlockBufferRef videoBlock = nullptr;
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      nullptr, packet.data.data, packet.data.size, kCFAllocatorNull, nullptr, 0,
      packet.data.size, 0, &videoBlock);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "create with memory block error:", status);
    return DecodeResult::dataError;
  }
  CMSampleBufferRef sampleBuffer = nullptr;
  const size_t sampleSizeArray[] = {(size_t)packet.data.size};
  CMSampleTimingInfo timingInfo = {};
  int32_t timeScale = 90000;
  timingInfo.presentationTimeStamp =
      CMTimeMakeWithSeconds(packet.pts, timeScale);
  // 喂真实 DTS: kCMTimeInvalid 时 VT 按投递顺序直接输出, B 帧流输出次序乱
  // (pts 回跳), 同步层逐帧判 jump 丢帧 → 卡顿。dts 无效才回退 kCMTimeInvalid
  if (packet.dts >= 0) {
    timingInfo.decodeTimeStamp = CMTimeMake(packet.dts, timeScale);
  } else {
    timingInfo.decodeTimeStamp = kCMTimeInvalid;
  }
  status = CMSampleBufferCreate(kCFAllocatorDefault, videoBlock, true, nullptr,
                                nullptr, videoFormatDescription, 1, 1,
                                &timingInfo, 1, sampleSizeArray, &sampleBuffer);
  if (status != noErr) {
    CFRelease(videoBlock);
    LOGFLF(LogLevel::warn, "buffer create error:", status);
    return DecodeResult::dataError;
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
  return status == noErr ? DecodeResult::success : DecodeResult::dataError;
}

void IOSVDecoder::flush() {
  if (decompressionSession) {
    // VTDecompressionSessionFlush(decompressionSession);
  }
}

void IOSVDecoder::onClose() {
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
      // 增加引用,放入队列,等待出队列后释放
      // videobuffer.cpp releaseGpuFrame
      CFRetain(imageBuffer);
      // log(LogLevel::info,"---now pts:",pts);
      // 使用 Metal 渲染
      GpuFrame frame = {};
      frame.pts = pts;
      frame.dts = frame.pts;
      frame.format = decoder->yuvFormat;
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

}
