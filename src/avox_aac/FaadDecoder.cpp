#ifdef AVOX_ENABLE_FAAD2

#include "FaadDecoder.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/player/MediaPlayer.hpp"

namespace avox {

void regFaadDecoder() {
  RegFunc faadReg = {"faad init", []() {
                       ACodecDesc faadDesc = {};
                       faadDesc.name = "faad decoder";
                       faadDesc.bHardware = false;
                       AvoxManager::Get().aDecoders.regInitFunc(
                           ACodecId::aac, faadDesc,
                           []() -> AudioDecoder* { return new FaadDecoder(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(faadReg);
}

FaadDecoder::FaadDecoder() { handle = NeAACDecOpen(); }

FaadDecoder::~FaadDecoder() { close(); }

bool FaadDecoder::onVaild() { return true; }

DecodeResult FaadDecoder::onPreDecoder() {
  // ffmpeg需要配置帧填充extradata
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
  unsigned long sampleRate = 0;
  uint8_t channels = 0;
  config = NeAACDecGetCurrentConfiguration(handle);
  // 原始通道数
  scrChannels = outDesc.channels;
  // 检查是否WebRTC调用了当前解码器
  if (bWebRtc) {
    // 强制S16输出
    config->outputFormat = FAAD_FMT_16BIT;
  }
  // 不要自动升采样(16000升32000)
  config->dontUpSampleImplicitSBR = 1;
  config->defObjectType = LC;
  config->downMatrix = 0;
  config->useOldADTSFormat = 0;
  NeAACDecSetConfiguration(handle, config);
  // NeAACDecInit2初始化有个二字节的音频信息，逻辑应该是如下
  // media-server\libflv\source\mpeg4-aac.c mpeg4_aac_audio_specific_config_load
  int32_t ret = 0;
  if (bAdts) {
    ret = NeAACDecInit(handle, confPkt->buff.data(), confPkt->buff.size(),
                       &sampleRate, &channels);
  } else {
    ret = NeAACDecInit2(handle, confPkt->buff.data(), confPkt->buff.size(),
                        &sampleRate, &channels);
  }
  outDesc.sampleRate = sampleRate;
  // faad会固定把单通道输出双通道
  outDesc.channels = channels;
  AvoxData confData = {confPkt->buff.data(), confPkt->buff.size(), true};
  if (bWebRtc) {
    // 因为webrtc有自己的渲染，所以通道数需要还原
    outDesc.channels = scrChannels;
  }
  outDesc.format = faadAudioFromat(config->outputFormat);
  log(LogLevel::info, "faad init out desc config:", confData,
      " webrtc:", bWebRtc, " out: ", outDesc,
      " object type:", (int32_t)config->defObjectType);
  // ret不为0表示初始化失败
  if (ret != 0) {
    LOGFLF(LogLevel::warn, "faad init failed,ret:", ret);
    return DecodeResult::openFailed;
  }
  return DecodeResult::success;
}

// 解码线程解码
DecodeResult FaadDecoder::decode(const AvoxPacket& packet) {
  if (!confPkt) {
    return DecodeResult::noConfig;
  }
  if (!config) {
    return onPreDecoder();
  }
  uint8_t* packetPtr = packet.data.data;
  int32_t dataSize = packet.data.size;
  if (bAdts) {
    // 如果用adts初始化，但是数据没adts头，自动添加adts头
    if (!bAdtsHeader(packetPtr, dataSize)) {
      const int32_t adtsHeaderSize = 7;
      if (aacData.size() < dataSize + adtsHeaderSize) {
        // 减少多次分配机率
        aacData.resize(dataSize * 2 + adtsHeaderSize);
      }
      // 添加adts头
      adtsHeader(aacData.data(), dataSize, confSampleIndex, confChannel,
                 objectType);
      // 复制数据
      memcpy(aacData.data() + adtsHeaderSize, packet.data.data, dataSize);
      packetPtr = aacData.data();
      dataSize = dataSize + adtsHeaderSize;
    }
  }
  NeAACDecFrameInfo info = {};
  uint8_t* outData =
      (uint8_t*)NeAACDecDecode(handle, &info, packetPtr, dataSize);
  if (info.error > 0) {
    LOGFLF(LogLevel::warn,
           "faad decode error:", NeAACDecGetErrorMessage(info.error));
    return DecodeResult::dataError;
  }
  // 这里的outDesc才正常
  if (!bAudioDescNotified) {
    LOGFLF(LogLevel::info, "adapt sample rate:", outDesc.sampleRate, "->",
           info.samplerate, " channels", (int32_t)outDesc.channels, "->",
           (int32_t)info.channels);
    outDesc.sampleRate = info.samplerate;
    outDesc.channels = info.channels;
    dispatch(&IAudioDecoderOb::onAudioDesc);
    bAudioDescNotified = true;
  }
  AvoxAFrame aframe = {};
  aframe.pts = packet.pts;
  aframe.buffer.data = outData;
  // samples包含通道数，经测试双通道固定2048，如果是SBR，samples会翻倍
  aframe.buffer.size = info.samples * audioFormatSize(outDesc.format);
  aframe.buffer.bRef = true;
  // webrtc如果原始是单通道，这里需要把解码后的双道数据取单通道数据
  if (bWebRtc && scrChannels == 1) {
    int16_t* ori = (int16_t*)aframe.buffer.data;
    int16_t* dest = (int16_t*)aframe.buffer.data;
    // LRLRLR 改为LLLLLLL
    for (int i = 0, j = 0; i < info.samples; i += 2, j += 1) {
      dest[j] = ori[i];
      // dest[j + 1024] = ori[i + 1];
    }
    // 双声道只取单声道，这里需要除以2
    // webrtc如果以uint16_t计算长度，后面需要再除以2
    aframe.buffer.size = aframe.buffer.size / 2;
  }
  // 这里的data还在，需要在onDecode用掉或是保存
  dispatch(&IAudioDecoderOb::onDecode, aframe);
  // log(LogLevel::info, "audio pts:", aframe.pts);
  return DecodeResult::success;
}

void FaadDecoder::flush() {}

void FaadDecoder::onClose() {
  if (handle) {
    NeAACDecClose(handle);
    handle = nullptr;
  }
  bAudioDescNotified = false;
}

}

#endif
