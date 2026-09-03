#include "RtcAudioEncoder.hpp"

#include "avox/module/AvoxManager.hpp"

namespace avox {

using namespace webrtc;

RtcAudioEncoder::RtcAudioEncoder(const AudioDesc& adesc_, int payType) {
  adesc = adesc_;
  payloadType = payType;
  findEncoder();
}

void RtcAudioEncoder::findEncoder() {
  bool bFind = AvoxManager::Get().aEncoders.hasObjectId(ACodecId::aac);
  if (!bFind) {
    log(LogLevel::info, "not find aac encoder");
    return;
  }
  const auto& encodes = AvoxManager::Get().aEncoders.initFuncs(ACodecId::aac);
  if (encodes.size() < 0) {
    return;
  }
  // 现在就用第一个来初始化编码器
  auto& aEncode = encodes[0];
  encode = std::unique_ptr<avox::AudioEncoder>(aEncode.initFunc());
  if (!encode) {
    return;
  }
  encode->setObserver(this);
  // 设置输入音频描述
  ATrackDesc trackDesc = {};
  trackDesc.desc = adesc;
  // 得到编码器输出描述
  encoderDesc = encode->setDesc(trackDesc);
  // 初始化编码器
  DecodeResult result = encode->onPreEncoder();
  if (result != DecodeResult::success) {
    log(LogLevel::info, "aac encoder init failed");
    encode.reset();
    return;
  }
}

RtcAudioEncoder::~RtcAudioEncoder() {
  if (encode) {
    encode->removeObserver(this);
    encode->onClose();
    encode.reset();
  }
}

int RtcAudioEncoder::SampleRateHz() const {
  return encoderDesc.desc.sampleRate;
}

size_t RtcAudioEncoder::NumChannels() const {
  return encoderDesc.desc.channels;
}

int RtcAudioEncoder::RtpTimestampRateHz() const {
  return encoderDesc.desc.sampleRate;
}

size_t RtcAudioEncoder::Num10MsFramesInNextPacket() const {
  // AAC 通常每帧 1024 个采样，对于 48kHz 是约 21.3ms
  // 对于 10ms 的帧，通常需要多个 10ms 帧才能组成一个编码包
  // 这里返回 1，表示每个 10ms 帧尝试编码一次
  return 1;
}

size_t RtcAudioEncoder::Max10MsFramesInAPacket() const {
  // 最大可以包含的 10ms 帧数
  return 1;
}

int RtcAudioEncoder::GetTargetBitrate() const {
  // 返回目标比特率，可以根据需要调整
  return 64000;  // 64kbps
}

void RtcAudioEncoder::Reset() {
  if (encode) {
    encode->flush();
    findEncoder();
  }
}

bool RtcAudioEncoder::SetFec(bool enable) {
  // FEC (Forward Error Correction) 支持
  return false;  // 暂时不支持
}

bool RtcAudioEncoder::SetDtx(bool enable) {
  // DTX (Discontinuous Transmission) 支持
  return false;  // 暂时不支持
}

bool RtcAudioEncoder::SetApplication(
    webrtc::AudioEncoder::Application application) {
  // 设置应用类型（语音/音频）
  return true;
}

void RtcAudioEncoder::SetMaxPlaybackRate(int frequency_hz) {
  // 设置最大播放速率
}

std::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>>
RtcAudioEncoder::GetFrameLengthRange() const {
  // 计算帧时间,aac每帧大多固定为1024
  double frame_time_ms = (1024.0 * 1000.0) / encoderDesc.desc.sampleRate;
  webrtc::TimeDelta duration =
      webrtc::TimeDelta::Millis(static_cast<int64_t>(frame_time_ms));
  // 因為 AAC 幀長通常是固定的，所以最小值和最大值相同
  return std::make_pair(duration, duration);
}

webrtc::AudioEncoder::EncodedInfo RtcAudioEncoder::EncodeImpl(
    uint32_t rtp_timestamp, webrtc::ArrayView<const int16_t> audio,
    webrtc::Buffer* encoded) {
  webrtc::AudioEncoder::EncodedInfo info;
  // 预设为 0，只有当 onPacket 被触发并填充了 encodedData 时才修改
  info.encoded_bytes = 0;
  if (!encode) {
    return info;
  }
  encodedData.clear();
  AvoxAFrame frame = {};
  frame.pts = rtp_timestamp * 1000 / adesc.sampleRate;
  frame.buffer.data =
      reinterpret_cast<uint8_t*>(const_cast<int16_t*>(audio.data()));
  frame.buffer.size = static_cast<int32_t>(audio.size() * sizeof(int16_t));
  frame.buffer.bRef = true;
  // 调用底层 fillFrame -> encode
  // 因为 fillFrame 内部会处理 curFrame 缓冲区，不满一帧时不会触发 onPacket
  encode->encode(frame);
  // 检查是否输出了编码包（在 onPacket 中填充）
  if (!encodedData.empty()) {
    encoded->AppendData(encodedData.data(), encodedData.size());
    info.encoded_bytes = encodedData.size();
    info.encoded_timestamp = currentRtpTimestamp;
    info.payload_type = payloadType;
  }
  return info;
}

void RtcAudioEncoder::onPacket(AvoxPacket& packet) {
  // 同步回调，直接保存编码后的数据
  // 注意：packet.data.bRef 可能为 true，需要复制数据
  if (packet.data.size > 0 && packet.data.data) {
    currentRtpTimestamp =
        static_cast<uint32_t>(packet.pts * encoderDesc.desc.sampleRate / 1000);
    size_t oldSize = encodedData.size();
    encodedData.resize(oldSize + packet.data.size);
    memcpy(encodedData.data() + oldSize, packet.data.data, packet.data.size);
  }
}

}
