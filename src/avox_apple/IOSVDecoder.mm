#include "IOSVDecoder.hpp"
#include "IOSHelper.h"
#include "avox/codec/H26XHelper.hpp"
#include <TargetConditionals.h>
#include <algorithm>
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
  // 重排状态随会话重建复位: 新流要重新学重排深度
  releaseReorder();
  maxPtsMinusDts = 0;
  reorderFrames = 0;
  frameDurMs = 0;
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
    // 10bit 家族探测: profile_idc ∈ {High10=110, High422=122, High444=244}。
    // SPS NAL 布局 [0]头+[1]profile+[2]constraint+[3]level, profile 字节处
    // EPB 不可能出现(同 h265 Main10 探测口径)。此前缺失 → hi10p 流恒按
    // 8bit 处理, VT 硬解静默折 8bit 交付 (yuvout-h264-hi10p 哨兵实证)
    streamBitDepth =
        (spsData.size() > 1 && (spsData[1] == 110 || spsData[1] == 122 ||
                                spsData[1] == 244))
            ? 10
            : 8;
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
  // Apple 平台 VT 的 H264 硬解只有 8bit NV12 输出能力: 10bit 请求 x420 会
  // "创建成功"但实际仍吐 '420v' 静默折 8bit (macOS 26 实证, 帧回调 pbType
  // 实测)。h264 High10 流直接放弃硬解车道 → 选型链回软解, 保住 10bit 交付
  // 契约 (yuvout-h264-hi10p 哨兵用例: expect yuv420P10)
  if (codecDesc.vcodecId == VCodecId::h264 && streamBitDepth == 10) {
    LOGFLF(LogLevel::warn, "vt h264 has no 10bit output, give up hw lane");
    return DecodeResult::openFailed;
  }
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
    // 10bit 流拒绝折 8bit 输出: 平台不支持 x420 时整个硬解车道放弃
    // (openFailed → 选型链回软解, 交付保住 yuv420P10); 旧的"回退 nv12 继续硬解"
    // 会静默把 10bit 折成 8bit, 违反交付契约 —— 哨兵 yuvout-h264-hi10p 的存在
    // 就是为抓这类隐性降质
    LOGFLF(LogLevel::warn, "x420 output unsupported, give up hw lane, status:",
           (int32_t)status);
    return DecodeResult::openFailed;
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
  // 重排深度: 包级 max(pts-dts) 就是"解码最多提前显示多少毫秒", 除以帧长即帧数。
  // 只在首个包前定帧长, 之后按 running max 更新(越靠后越准)
  if (frameDurMs <= 0) {
    frameDurMs = srcDesc.fps > 1.0 ? (int64_t)(1000.0 / srcDesc.fps + 0.5) : 40;
    if (frameDurMs <= 0) {
      frameDurMs = 40;
    }
  }
  if (packet.dts >= 0 && packet.pts >= 0) {
    // 回调线程会读 reorderFrames, 写入需互斥
    std::lock_guard<std::mutex> lk(reorderMutex);
    maxPtsMinusDts = std::max(maxPtsMinusDts, packet.pts - packet.dts);
    reorderFrames = std::min<int64_t>(16, maxPtsMinusDts / frameDurMs);
  }
  timingInfo.presentationTimeStamp =
      CMTimeMakeWithSeconds(packet.pts, timeScale);
  // 喂真实 DTS: kCMTimeInvalid 时 VT 按投递顺序直接输出, B 帧流输出次序乱
  // (pts 回跳), 同步层逐帧判 jump 丢帧 → 卡顿。dts 无效才回退 kCMTimeInvalid
  // 单位必须与 pts 同源: packet.pts/dts 都是毫秒, 两者都用「秒」构造。
  // 混用 CMTimeMake 会把 dts 当 90000 分之一秒, 压缩成 0~0.0015s 的假解码轴,
  // VT 据它算不出重排深度 → 即使开了 EnableTemporalProcessing 也照投递序吐帧
  // (2026-09-24 实证: 投递序与回调序 pts 序列逐项相同)
  if (packet.dts >= 0) {
    timingInfo.decodeTimeStamp = CMTimeMakeWithSeconds(packet.dts, timeScale);
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
  // 只开异步解码时 VT 按投递顺序(解码序)吐帧, B 帧流会 I P B B B 依次上屏
  // = 画面往复; 开时间处理让解码器缓存到够数后按显示序输出(2026-09-24 实证)
  VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression |
                             kVTDecodeFrame_EnableTemporalProcessing;
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
  // seek 语义: 扣住的重排帧属于旧位置, 丢弃而不是放出
  releaseReorder();
  if (decompressionSession) {
    // VTDecompressionSessionFlush(decompressionSession);
  }
}

void IOSVDecoder::onInputEnd() {
  // 输入排空: 放空重排缓冲尾部, 否则最后 reorderFrames 帧永不显示
  flushReorder();
}

void IOSVDecoder::onClose() {
  if (decompressionSession) {
    // 先等异步回调收完: 否则回调线程可能正往重排缓冲里 push, 与下面的释放相撞
    if (getIosDeviceSystemVersion() >= 11) {
      VTDecompressionSessionWaitForAsynchronousFrames(decompressionSession);
    }
    VTDecompressionSessionInvalidate(decompressionSession);
    CFRelease(decompressionSession);
    decompressionSession = nullptr;
  }
  releaseReorder();
}

void IOSVDecoder::dispatchDecodedFrame(int64_t pts, CVImageBufferRef imageBuffer) {
  if (bMetalRender) {
    // 引用转交下游释放(videobuffer.cpp releaseGpuFrame)
    GpuFrame frame = {};
    frame.pts = pts;
    frame.dts = pts;
    frame.format = yuvFormat;
    frame.buffer = imageBuffer;
    dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
    return;
  }
  YUVFrame frame = {};
  frame.pts = pts;
  frame.dts = pts;
  frame.format = yuvFormat;
  // 锁定图像缓冲区的基地址以便访问数据
  CVPixelBufferLockBaseAddress(imageBuffer, 0);
  size_t planeCount = CVPixelBufferGetPlaneCount(imageBuffer);
  for (size_t i = 0; i < planeCount; ++i) {
    frame.data[i] = static_cast<uint8_t *>(
        CVPixelBufferGetBaseAddressOfPlane(imageBuffer, i));
    frame.stride[i] = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, i);
  }
  // 解锁图像缓冲区的基地址
  CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
  dispatch(&IVideoDecoderOb::onDecode, frame);
  CFRelease(imageBuffer);
}

void IOSVDecoder::drainReorder() {
  std::vector<ReorderFrame> ready;
  {
    // 只在本锁内挑帧; 投递在锁外做——下游 enqueueWait 可能阻塞, 持锁会与
    // 解码线程的 flush/onInputEnd 互等
    std::lock_guard<std::mutex> lk(reorderMutex);
    while ((int64_t)reorderBuf.size() > reorderFrames) {
      auto it = std::min_element(reorderBuf.begin(), reorderBuf.end(),
                                 [](const ReorderFrame &a, const ReorderFrame &b) {
                                   return a.pts < b.pts;
                                 });
      ready.push_back(*it);
      reorderBuf.erase(it);
    }
  }
  for (const ReorderFrame &f : ready) {
    dispatchDecodedFrame(f.pts, f.buffer);
  }
}

void IOSVDecoder::flushReorder() {
  std::vector<ReorderFrame> out;
  {
    std::lock_guard<std::mutex> lk(reorderMutex);
    if (reorderBuf.empty()) {
      return;
    }
    std::stable_sort(reorderBuf.begin(), reorderBuf.end(),
                     [](const ReorderFrame &a, const ReorderFrame &b) {
                       return a.pts < b.pts;
                     });
    out.swap(reorderBuf);
  }
  for (const ReorderFrame &f : out) {
    dispatchDecodedFrame(f.pts, f.buffer);
  }
}

void IOSVDecoder::releaseReorder() {
  std::vector<ReorderFrame> drop;
  {
    std::lock_guard<std::mutex> lk(reorderMutex);
    drop.swap(reorderBuf);
  }
  for (const ReorderFrame &f : drop) {
    CFRelease(f.buffer);
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
  if (!decoder) {
    return;
  }
  // 回调按解码序到达, 先扣住引用, 由 drainReorder 按 pts 序放行
  CFRetain(imageBuffer);
  {
    std::lock_guard<std::mutex> lk(decoder->reorderMutex);
    decoder->reorderBuf.push_back({pts, imageBuffer});
  }
  decoder->drainReorder();
}

}
