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
        // VP9(webm): onVaild 探测系统版本与 VT 硬解支持, 不支持回退软解
        codecDesc = {};
        codecDesc.name = AVOX_IOS_VP9_DECODER;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::vp9;
        AvoxManager::Get().vDecoders.regInitFunc(
            VCodecId::vp9, codecDesc, []() -> VideoDecoder * {
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
    // x420 与 P010 同布局(16bit 字高位对齐 10bit), 渲染/导出侧按 p010 语义处理
    yuvFormat.type = streamBitDepth == 10 ? YuvType::p010 : YuvType::nv12;
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
  if (codecDesc.vcodecId == VCodecId::vp9) {
    // VP9: iOS 14+ / macOS 11+, 且需运行期确认硬解能力; 不满足回退软解
    bMustVcc = false;  // 无 avcc/hvcc 语义, annexb 重排会破坏 in-band 帧
#if TARGET_OS_OSX
    const float kVp9MinVersion = 11.0;
#else
    const float kVp9MinVersion = 14.0;
#endif
    if (version < kVp9MinVersion) {
      LOGFLF(LogLevel::warn, "ios version :", version,
             " not support vp9 decoder");
      return false;
    }
    if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_VP9)) {
      LOGFLF(LogLevel::warn, "vp9 hardware decode not supported on this device");
      return false;
    }
  }
  return true;
}

DecodeResult IOSVDecoder::onPreDecoder() {
  const auto &packets = configPackets;
  // 每次会话重建重置, 防解码器复用时上次流的位深残留
  streamBitDepth = 8;
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
  } else if (codecDesc.vcodecId == VCodecId::h265) {
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
    // Main10 探测: SPS NAL 无起始码布局为 [0..1]头+[2]vpsid/子层+[3]profile,
    // 低5位 profile_idc==2 即 Main10。EPB 不可能落在此处([3]==0 即 profile_idc=0
    // 非法流), 无需展开仿真预防字节直接判
    streamBitDepth = (spsData.size() > 3 && (spsData[3] & 0x1F) == 2) ? 10 : 8;
    const uint8_t *const parameterSetPointers[3] = {
        vpsData.data(), spsData.data(), ppsData.data()};
    const size_t parameterSetSizes[3] = {vpsData.size(), spsData.size(),
                                         ppsData.size()};
    status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
        kCFAllocatorDefault, 3, parameterSetPointers, parameterSetSizes, 4,
        nullptr, &videoFormatDescription);
  } else if (codecDesc.vcodecId == VCodecId::vp9) {
    // VP9: 无带外参数集(in-band), 直接建无 extensions 的格式描述;
    // 宽高采信容器侧 srcDesc(经 parseConfigs 填充)
    if (packets.empty()) {
      return DecodeResult::noConfig;
    }
    if (!parseConfigs() || params.width <= 0 || params.height <= 0) {
      LOGFLF(LogLevel::warn, "vp9 missing valid dimensions in container");
      return DecodeResult::noConfig;
    }
    status = CMVideoFormatDescriptionCreate(
        kCFAllocatorDefault, kCMVideoCodecType_VP9, params.width, params.height,
        nullptr, &videoFormatDescription);
  }
  if (status != errSecSuccess || !videoFormatDescription) {
    LOGFLF(LogLevel::warn,
           "open ios hard decoder open videoformatdescription failed");
    return DecodeResult::openFailed;
  }
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
  // x420 kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange: 10bit 流直接输出
  // 10bit(P010 布局), 8bit NV12 会截断 PQ 编码损失精度(渲染侧 tone map 依赖
  // 完整 PQ 码值); 平台不支持 x420 输出时回退 NV12
  // 不带 kCVPixelBufferOpenGLESCompatibilityKey: 本解码器恒 Metal 渲染用不到
  // GLES 兼容, 且 iOS 26 已移除 OpenGL ES, 带 GLES 兼容键建会话会挂在废弃
  // GL 路径上 (真机实测视频解码线程停在会话创建, 包队列积压零帧)
  OSType dstFmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  if (codecDesc.vcodecId == VCodecId::h265 && streamBitDepth == 10) {
    dstFmt = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
  }
  NSDictionary *attr = [NSDictionary
      dictionaryWithObjectsAndKeys: [NSNumber numberWithInt:dstFmt],
          (id)kCVPixelBufferPixelFormatTypeKey, nil];
  LOGFLF(LogLevel::info, "creating ios vt decompression session");
  status = VTDecompressionSessionCreate(
      kCFAllocatorDefault, videoFormatDescription, nullptr,
      (__bridge CFDictionaryRef)attr, &callback, &decompressionSession);
  if (status != noErr && dstFmt != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
    LOGFLF(LogLevel::warn, "x420 output unsupported, fallback nv12, status:",
           (int32_t)status);
    streamBitDepth = 8;
    dstFmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    attr = [NSDictionary
        dictionaryWithObjectsAndKeys: [NSNumber numberWithInt:dstFmt],
            (id)kCVPixelBufferPixelFormatTypeKey, nil];
    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault, videoFormatDescription, nullptr,
        (__bridge CFDictionaryRef)attr, &callback, &decompressionSession);
  }
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
  // 合并组首NAL可能是PPS/SEI/AUD等非VCL而组内仍含slice, 不能按首NAL整包丢:
  // HDR素材每组带前导SEI(元数据), 首NAL判会全量丢帧(实证硬解零帧)。VT 能自行
  // 解析组内非VCL前缀, 整组喂入; 拆分后无任何帧NAL才丢
  if (!bDecode) {
    std::vector<AvoxPacket> nalus;
    splitAvccNalu(packet, nalus);
    for (const auto &item : nalus) {
      if (naluDataFrame(codecDesc.vcodecId, getNalUnit(codecDesc.vcodecId, item))) {
        bDecode = true;
        break;
      }
    }
  }
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
