#ifdef AVOX_ENABLE_FDKAAC

#include "FdkaacEncoder.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

void regFdkaacEncoder() {
  RegFunc faadReg = {"fdk-aac init", []() {
                       ACodecDesc faadDesc = {};
                       faadDesc.name = "fdk-aac encoder";
                       faadDesc.bHardware = false;
                       AvoxManager::Get().aEncoders.regInitFunc(
                           ACodecId::aac, faadDesc, []() -> AudioEncoder* {
                             return new FdkaacEncoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(faadReg);
}

FdkaacEncoder::FdkaacEncoder() {}

FdkaacEncoder::~FdkaacEncoder() {
  if (handle) {
    aacEncClose(&handle);
    handle = nullptr;
  }
}

AudioDesc FdkaacEncoder::getSupportDesc(const AudioDesc& desc) {
  outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  return outDesc;
}

DecodeResult FdkaacEncoder::onPreEncoder() {
  if (handle) {
    aacEncClose(&handle);
    handle = nullptr;
  }
  inputChannels = outDesc.channels;
  inputSampleRate = outDesc.sampleRate;
  if (aacEncOpen(&handle, 0, inputChannels) != AACENC_OK) {
    LOGFLF(LogLevel::error, "aacEncOpen failed");
    return DecodeResult::openFailed;
  }
  // 配置编码器参数
  aacEncoder_SetParam(handle, AACENC_AOT, 2);  // AAC LC
  aacEncoder_SetParam(handle, AACENC_SAMPLERATE, inputSampleRate);
  aacEncoder_SetParam(handle, AACENC_CHANNELMODE,
                      inputChannels == 1 ? MODE_1 : MODE_2);
  aacEncoder_SetParam(handle, AACENC_BITRATE, 128000);
  // 0-raw, 1-ADIF, 2-ADTS
  aacEncoder_SetParam(handle, AACENC_TRANSMUX, 0);
  aacEncoder_SetParam(handle, AACENC_AFTERBURNER, 1);
  if (aacEncEncode(handle, nullptr, nullptr, nullptr, nullptr) != AACENC_OK) {
    LOGFLF(LogLevel::error, "init failed");
    return DecodeResult::openFailed;
  }
  if (aacEncInfo(handle, &info) != AACENC_OK) {
    LOGFLF(LogLevel::error, "aacEncInfo failed");
    return DecodeResult::openFailed;
  }
  // 16bit
  curFrame.setSize(info.frameLength * inputChannels * 2);
  curFrame.setPts(AVOX_NOVALID_PTS);
  aacData.resize(curFrame.getSize() * 2);
  LOGFLF(LogLevel::info, "curFrame size:", curFrame.getSize(),
         " inputChannels:", inputChannels,
         " inputSampleRate:", inputSampleRate);
  return DecodeResult::success;
}

DecodeResult FdkaacEncoder::encode(const AvoxAFrame& frame) {
  if (!handle) {
    DecodeResult initRet = onPreEncoder();
    if (initRet != DecodeResult::success) {
      return initRet;
    }
  }
  // LOGFLF(LogLevel::info, "encode audio pts:", frame.pts);
  return fillFrame(frame);
}

DecodeResult FdkaacEncoder::encode() {
  AACENC_BufDesc inBuf = {0}, outBuf = {0};
  AACENC_InArgs inArgs = {0};
  AACENC_OutArgs outArgs = {0};

  int16_t* inputBuf = (int16_t*)curFrame.point();
  int inBufSize = curFrame.getSize();
  int inSamples = inBufSize / sizeof(int16_t);

  void* inPtrs[] = {inputBuf};
  int inBufIds[] = {IN_AUDIO_DATA};
  int inBufSizes[] = {inBufSize};
  int inBufElSizes[] = {sizeof(int16_t)};

  inBuf.numBufs = 1;
  inBuf.bufs = inPtrs;
  inBuf.bufferIdentifiers = inBufIds;
  inBuf.bufSizes = inBufSizes;
  inBuf.bufElSizes = inBufElSizes;

  inArgs.numInSamples = inSamples;

  void* outPtrs[] = {aacData.data()};
  int outBufIds[] = {OUT_BITSTREAM_DATA};
  int outBufSizes[] = {(int)aacData.size()};
  int outBufElSizes[] = {1};

  outBuf.numBufs = 1;
  outBuf.bufs = outPtrs;
  outBuf.bufferIdentifiers = outBufIds;
  outBuf.bufSizes = outBufSizes;
  outBuf.bufElSizes = outBufElSizes;

  if (aacEncEncode(handle, &inBuf, &outBuf, &inArgs, &outArgs) != AACENC_OK) {
    log(LogLevel::warn, "FdkaacEncoder: aacEncEncode failed");
    return DecodeResult::dataError;
  }
  if (outArgs.numOutBytes == 0) {
    return DecodeResult::dataNoReady;
  }
  if (outArgs.numOutBytes < 0) {
    log(LogLevel::warn, "faacEncEncode failed");
    return DecodeResult::dataError;
  }
  AvoxPacket cpacket = {};
  cpacket.data = {aacData.data(), outArgs.numOutBytes, true};
  cpacket.pts = curFrame.getPts();
  cpacket.dts = cpacket.pts;
  cpacket.packtype = (int32_t)PackType::audio;
  cpacket.index = 0;
  dispatch(&IEncoderOb::onPacket, cpacket);
  return DecodeResult::success;
}

}
#endif