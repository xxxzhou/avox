#ifdef AVOX_ENABLE_FDKAAC

#include "FdkaacDecoder.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// 注册fdk-aac解码器
void regFdkaacDecoder() {
  RegFunc fdkReg = {"fdk-aac decoder init", []() {
                      ACodecDesc fdkDesc = {};
                      fdkDesc.name = "fdk-aac decoder";
                      fdkDesc.bHardware = false;
                      AvoxManager::Get().aDecoders.regInitFunc(
                          ACodecId::aac, fdkDesc, []() -> AudioDecoder* {
                            return new FdkaacDecoder();
                          });
                    }};
  AvoxManager::Get().initFuncs.push_back(fdkReg);
}

FdkaacDecoder::FdkaacDecoder() {
  // LOGFLF(LogLevel::info, "FdkaacDecoder created");
}

FdkaacDecoder::~FdkaacDecoder() { onClose(); }

bool FdkaacDecoder::onVaild() { return true; }

DecodeResult FdkaacDecoder::onPreDecoder() {
  if (!confPkt) {
    return DecodeResult::noConfig;
  }
  // 如果配置使用adts头，则解析数据也需要带adts头，zlmediakt直播流
  // 如果配置文件使用asc头，则解析数据不带头，本地流,webrtc,ffmpeg解析流
  // adts头与ASC(Audio Specific Config)头
  AacSC aacsc = {};
  splitAAConfig(*confPkt, aacsc, bAdts);
  objectType = aacsc.objectType;
  confSampleIndex = aacsc.confSampleIndex;
  confChannel = aacsc.confChannel;
  // 根据配置类型选择传输类型
  TRANSPORT_TYPE transportType = bAdts ? TT_MP4_ADTS : TT_MP4_RAW;
  // 创建解码器实例
  handle = aacDecoder_Open(transportType, 1);
  if (!handle) {
    LOGFLF(LogLevel::error, "failed to open fdk-aac decoder");
    return DecodeResult::openFailed;
  }
  // 设置配置数据
  if (!bAdts && confPkt->buff.size() > 0) {
    // 对于ASC配置，需要设置到解码器
    UCHAR* configData = confPkt->buff.data();
    UINT configSize = confPkt->buff.size();
    AAC_DECODER_ERROR err =
        aacDecoder_ConfigRaw(handle, &configData, &configSize);
    if (err != AAC_DEC_OK) {
      LOGFLF(LogLevel::error, "Failed to set ASC config to decoder");
      aacDecoder_Close(handle);
      handle = nullptr;
      return DecodeResult::openFailed;
    }
  }
  // 设置DRC参数（动态范围控制）
  if (aacDecoder_SetParam(handle, AAC_DRC_REFERENCE_LEVEL, -1) != AAC_DEC_OK) {
    LOGFLF(LogLevel::warn, "Failed to set DRC reference level");
    aacDecoder_Close(handle);
    handle = nullptr;
    return DecodeResult::openFailed;
  }
  decodeBuffer.resize(10240);
  // 获取流信息
  CStreamInfo* streamInfo = aacDecoder_GetStreamInfo(handle);
  // 初始化采样率和声道（可能在首次解码后才准确，特别是 HE-AAC）
  // 先使用配置值，后续解码时会更新为实际值
  int32_t configSampleRate = getSampleRateByIndex(confSampleIndex);
  outDesc.sampleRate = configSampleRate;
  outDesc.channels = confChannel;
  // 如果 streamInfo 有效，使用实际值
  if (streamInfo && streamInfo->sampleRate > 0) {
    outDesc.sampleRate = streamInfo->sampleRate;
    outDesc.channels = streamInfo->numChannels;
  }
  // 固定解码输出S16
  outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  LOGFLF(LogLevel::info, "initialized successfully with ",
         bAdts ? "ADTS" : "ASC", " mode, confSampleIndex:", confSampleIndex,
         " configSampleRate:", configSampleRate,
         " output sampleRate:", outDesc.sampleRate,
         " channels:", outDesc.channels);
  return DecodeResult::success;
}

DecodeResult FdkaacDecoder::decode(const AvoxPacket& packet) {
  if (!confPkt) {
    return DecodeResult::noConfig;
  }
  if (!handle) {
    DecodeResult result = onPreDecoder();
    if (result != DecodeResult::success) {
      return result;
    }
    // onPreDecoder 成功后继续解码当前包
    // 不要 return，继续执行下面的解码逻辑
  }
  // 处理ADTS模式下的数据
  UCHAR* dataPtr = packet.data.data;
  UINT dataSize = packet.data.size;
  if (bAdts) {
    // 纯adts头包(配置拆出的7/9字节)不喂fdk: 流模式Fill是追加, 残头留在内部
    // 缓冲会令下一帧错位解析, 解出垃圾报error:5后持续失步
    if (dataSize <= 9 && bAdtsHeader(dataPtr, dataSize)) {
      return DecodeResult::dataNoReady;
    }
    // ADTS模式：检查数据是否有ADTS头，如果没有需要添加
    if (!bAdtsHeader(dataPtr, dataSize)) {
      // 需要添加ADTS头
      const int32_t adtsHeaderSize = 7;
      if (aacData.size() < dataSize + adtsHeaderSize) {
        aacData.resize(dataSize * 2 + adtsHeaderSize);
      }
      adtsHeader(aacData.data(), dataSize, confSampleIndex, confChannel,
                 objectType);
      // 复制音频数据
      memcpy(aacData.data() + adtsHeaderSize, dataPtr, dataSize);
      dataPtr = aacData.data();
      dataSize = dataSize + adtsHeaderSize;
    }
  }
  // 填充数据到解码器
  UINT valid = dataSize;
  AAC_DECODER_ERROR err = aacDecoder_Fill(handle, &dataPtr, &dataSize, &valid);
  if (err != AAC_DEC_OK) {
    handleDecodeError(err);
    return DecodeResult::dataError;
  }
  // 解码帧
  INT_PCM* outputBuffer = reinterpret_cast<INT_PCM*>(decodeBuffer.data());
  err = aacDecoder_DecodeFrame(handle, outputBuffer,
                               decodeBuffer.size() / sizeof(INT_PCM), 0);

  if (err != AAC_DEC_OK) {
    if (err == AAC_DEC_NOT_ENOUGH_BITS) {
      return DecodeResult::dataNoReady;
    }
    handleDecodeError(err);
    return DecodeResult::dataError;
  }
  if (frameSize <= 0) {
    // 获取流信息
    CStreamInfo* streamInfo = aacDecoder_GetStreamInfo(handle);
    // 更新实际的采样率和声道数（HE-AAC 可能翻倍）
    if (streamInfo->sampleRate != outDesc.sampleRate ||
        streamInfo->numChannels != outDesc.channels) {
      LOGFLF(LogLevel::info, "update sample rate from ", outDesc.sampleRate,
             " to ", streamInfo->sampleRate, " channels from ",
             outDesc.channels, " to ", streamInfo->numChannels);
      outDesc.sampleRate = streamInfo->sampleRate;
      outDesc.channels = streamInfo->numChannels;
    }
    // 初始化解码缓冲区
    frameSize = streamInfo->frameSize;
    // HE-AAC 实际采样率在首次解码后确定，需要通知 AudioDesc 更新
    // 必须在 onDecode 之前调用 onAudioDesc，否则 frameSize 还是 0
    if (!bAudioDescNotified) {
      bAudioDescNotified = true;      
      dispatch(&IAudioDecoderOb::onAudioDesc);
    }
  }
  AvoxAFrame aframe = {};
  aframe.pts = packet.pts;
  aframe.buffer.data = decodeBuffer.data();
  aframe.buffer.size = frameSize * outDesc.channels * 2;
  aframe.buffer.bRef = true;
  // 分发解码后的音频帧
  dispatch(&IAudioDecoderOb::onDecode, aframe);
  return DecodeResult::success;
}

void FdkaacDecoder::flush() {
  if (handle) {
    // FDK AAC 的 AACDEC_FLUSH 需要多次调用才能完全清空内部缓冲区
    // 每次调用会返回一个残留帧，直到返回 AAC_DEC_NOT_ENOUGH_BITS 表示已清空
    // HE-AAC 使用 SBR 技术，内部可能缓冲 30+ 帧
    UINT flags = AACDEC_FLUSH;
    std::vector<uint8_t> flushBuffer(10240);
    INT_PCM* outputBuffer = reinterpret_cast<INT_PCM*>(flushBuffer.data());
    AAC_DECODER_ERROR err = AAC_DEC_OK;
    int flushCount = 0;
    // 最多 flush 100 次，避免无限循环
    while (flushCount < 10) {
      err = aacDecoder_DecodeFrame(handle, outputBuffer,
                                    flushBuffer.size() / sizeof(INT_PCM), flags);
      flushCount++;
      if (err == AAC_DEC_NOT_ENOUGH_BITS || err == AAC_DEC_TRANSPORT_SYNC_ERROR) {
        // 解码器内部缓冲区已清空
        break;
      }
    }
    LOGFLF(LogLevel::info, "fdk-aac flush, count:", flushCount,
           " last err:", (int)err);
  }
}

void FdkaacDecoder::onClose() {
  if (handle) {
    aacDecoder_Close(handle);
    handle = nullptr;
  }
  decodeBuffer.clear();
  aacData.clear();
  frameSize = 0;
  bAudioDescNotified = false;
}

void FdkaacDecoder::handleDecodeError(AAC_DECODER_ERROR error) {
  lastError = error;
  const char* errorDesc = getErrorDescription(error);
  LOGFLF(LogLevel::warn, "decode error:", error, " - ", errorDesc);
}

const char* FdkaacDecoder::getErrorDescription(AAC_DECODER_ERROR error) {
  switch (error) {
    case AAC_DEC_OK:
      return "No error";
    case AAC_DEC_INVALID_HANDLE:
      return "Invalid handle";
    case AAC_DEC_UNSUPPORTED_AOT:
      return "Unsupported audio object type";
    case AAC_DEC_UNSUPPORTED_FORMAT:
      return "Unsupported format";
    case AAC_DEC_UNSUPPORTED_ER_FORMAT:
      return "Unsupported error resilience format";
    case AAC_DEC_UNSUPPORTED_MULTILAYER:
      return "Unsupported multilayer";
    case AAC_DEC_UNSUPPORTED_CHANNELCONFIG:
      return "Unsupported channel configuration";
    case AAC_DEC_UNSUPPORTED_SAMPLINGRATE:
      return "Unsupported sampling rate";
    case AAC_DEC_UNSUPPORTED_EXTENSION_PAYLOAD:
      return "Unsupported extension payload";
    case AAC_DEC_OUT_OF_MEMORY:
      return "Out of memory";
    case AAC_DEC_TRANSPORT_SYNC_ERROR:
      return "Transport sync error";
    case AAC_DEC_PARSE_ERROR:
      return "Parse error";
    case AAC_DEC_CRC_ERROR:
      return "CRC error";
    case AAC_DEC_RVLC_ERROR:
      return "RVLC error";
    default:
      return "Unknown error";
  }
}

}
#endif