#ifdef AVOX_ENABLE_FAAC

#include "FaacEncoder.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

void regFaacEncoder() {
  RegFunc faadReg = {"faac init", []() {
                       ACodecDesc faadDesc = {};
                       faadDesc.name = "faac encoder";
                       faadDesc.bHardware = false;
                       AvoxManager::Get().aEncoders.regInitFunc(
                           ACodecId::aac, faadDesc, []() -> AudioEncoder * {
                             return new FaacEncoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(faadReg);
}

FaacEncoder::FaacEncoder() {}

FaacEncoder::~FaacEncoder() {
  if (handle) {
    faacEncClose(handle);
    handle = nullptr;
  }
}

AudioDesc FaacEncoder::getSupportDesc(const AudioDesc &desc) {
  // 返回支持的音频格式描述  
  outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  return outDesc;
}

DecodeResult FaacEncoder::onPreEncoder() {
  if (handle) {
    faacEncClose(handle);
    handle = nullptr;
  }
  // 经测试，这个inputSamples包含声道数
  inputSamples = 0;
  maxOutputBytes = 0;
  // 假设 audioDesc 已经设置
  handle = faacEncOpen(enDesc.desc.sampleRate, enDesc.desc.channels,
                       &inputSamples, &maxOutputBytes);
  if (!handle) {
    log(LogLevel::error, "faacEncOpen failed");
    return DecodeResult::openFailed;
  }
  faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(handle);
  config->inputFormat = FAAC_INPUT_16BIT;
  // ADTS
  config->outputFormat = 0;
  config->useLfe = 0;
  config->aacObjectType = LOW;
  // config->mpegVersion = MPEG4;
  faacEncSetConfiguration(handle, config);
  // 16bit
  curFrame.setSize(inputSamples * 2);
  curFrame.setPts(AVOX_NOVALID_PTS);
  aacData.resize(maxOutputBytes);
  return DecodeResult::success;
}

DecodeResult FaacEncoder::encode(const AvoxAFrame &frame) {
  if (!handle) {
    DecodeResult initRet = onPreEncoder();
    if (initRet != DecodeResult::success) {
      return initRet;
    }
  }
  return fillFrame(frame);
}

DecodeResult FaacEncoder::encode() {
  int bytesEncoded =
      faacEncEncode(handle, (int32_t *)curFrame.point(), inputSamples,
                    aacData.data(), maxOutputBytes);
  if (bytesEncoded == 0) {
    return DecodeResult::dataNoReady;
  }
  if (bytesEncoded < 0) {
    log(LogLevel::warn, "faacEncEncode failed");
    return DecodeResult::dataError;
  }
  AvoxPacket cpacket = {};
  cpacket.data = {aacData.data(), bytesEncoded, true};
  cpacket.pts = curFrame.getPts();
  cpacket.dts = cpacket.pts;
  cpacket.packtype = (int32_t)PackType::audio;
  cpacket.index = 0;
  dispatch(&IEncoderOb::onPacket, cpacket);
  // 空实现，按需补充
  return DecodeResult::success;
}

}

#endif