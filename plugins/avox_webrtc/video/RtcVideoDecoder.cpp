#include "RtcVideoDecoder.hpp"

#include "RtcVideoBuffer.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/AVTrack.hpp"
#include "common_video/h264/h264_bitstream_parser.h"
#include "common_video/h265/h265_bitstream_parser.h"
#include "common_video/include/video_frame_buffer.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace avox {

using namespace webrtc;

RtcVideoDecoder::RtcVideoDecoder() {}

RtcVideoDecoder::~RtcVideoDecoder() {}

bool RtcVideoDecoder::Configure(const Settings& settings) {
  LOGFLF(LogLevel::info, "number_of_cores:", settings.number_of_cores());
  if (settings.codec_type() == kVideoCodecH264) {
    codecId = VCodecId::h264;
  } else if (settings.codec_type() == kVideoCodecH265) {
    codecId = VCodecId::h265;
  } else {
    return false;
  }
  bool bHard = true;
  const char* sName = getDefaultDecoderName(codecId, bHard);
  // 查找解码器列表
  const auto& decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.empty()) {
    return false;
  }
  size_t sIndex = 0;
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto& vDecode = decodes[sIndex];
  // 初始化解码器
  decode = std::unique_ptr<avox::VideoDecoder>(vDecode.initFunc());
  if (!decode) {
    return false;
  }
  if (codecId == VCodecId::h264) {
    parser = std::make_unique<H264BitstreamParser>();
  } else {
    parser = std::make_unique<H265BitstreamParser>();
  }
  decode->setObserver(this);
  configType = ConfigAddType::none;
  VideoDesc videoDesc = {};
  videoDesc.width = settings.max_render_resolution().Width();
  videoDesc.height = settings.max_render_resolution().Height();
  codecDesc = vDecode.desc;
  return decode->setContext(codecDesc, videoDesc);
}

int32_t RtcVideoDecoder::Release() {
  if (decode) {
    decode->removeObserver(this);
    decode->close();
    decode.reset();
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

void RtcVideoDecoder::processPacket(AvoxPacket& packet) {
  uint8_t nal = getNalUnit(codecId, packet);  
  bool bConfig = naluConfigFrame(codecId, nal);
  bool bKeyFrame = naluKeyFrame(codecId, nal);
  // LOGFLF(LogLevel::info, "nalu type:", getNalName(codecId, nal),
  //        " key:", bKeyFrame);
  if (bConfig) {
    packet.packtype = (int32_t)PackType::vconfig;
    ConfigAddType ctype = decode->pushConfig(packet);
    if (ctype == ConfigAddType::updateSize || ctype == ConfigAddType::update) {
      configType = ctype;
    }
  } else if (bKeyFrame) {
    // 如果配置帧更新,重置解码器
    if (configType == ConfigAddType::update ||
        configType == ConfigAddType::updateSize) {
      configType = ConfigAddType::none;    
      if(decode){
        decode->flush();
      }
      DecodeResult result = decode->onPreDecoder();      
      if (result == DecodeResult::success) {
        LOGFLF(LogLevel::info, "reset decoder success");
      } else {
        LOGFLF(LogLevel::warn, "reset decoder fail ");
      }
    }
  }
}

int32_t RtcVideoDecoder::Decode(const webrtc::EncodedImage& input_image,
                                bool missing_frames, int64_t render_time_ms) {
  if (!decode) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  // 1. 局部变量解析 QP，避免成员变量污染
  std::optional<uint8_t> current_qp = std::nullopt;
  if (parser) {
    parser->ParseBitstream(input_image);
    int qp_val = parser->GetLastSliceQp().value_or(-1);
    if (qp_val >= 0) {
      current_qp = static_cast<uint8_t>(qp_val);
    }
  }
  // 2. 使用锁保护并存储映射关系
  {
    std::lock_guard<std::mutex> lock(infoMutex);
    ImageTimeInfo info = {};
    info.pts = render_time_ms;
    info.rtpTimestamp = input_image.RtpTimestamp();
    info.qp = current_qp;
    imageTimeQueue.push_back(info);
    // 保持队列长度，防止异常堆积
    if (imageTimeQueue.size() > 64) {
      imageTimeQueue.pop_front();
    }
  }
  // 3. 准备解码包
  AvoxPacket packet = {};
  packet.pts = render_time_ms;
  packet.dts = render_time_ms;
  packet.index = 0;
  packet.data.data = const_cast<uint8_t*>(input_image.data());
  packet.data.size = input_image.size();
  packet.prefixSize = 4;
  webrtc::VideoFrameType frameType = input_image.FrameType();
  packet.frameType =
      frameType == webrtc::VideoFrameType::kVideoFrameKey ? 1 : 0;
  // if (packet.frameType == 1) {
  //   LOGFLF(LogLevel::info, "key frame");
  // }
  uint8_t nalu = getNalUnit(codecId, packet);
  // AUD/SEI后面都可能带配置帧,需要解包查看
  bool bConfig = naluConfigFrame(codecId, nalu);
  // AUD/SEI可丢弃帧
  bool bDrop = naluDropAble(codecId, nalu);
  // 检查是否有配置帧
  if (bConfig || bDrop) {
    splitAnnexbNalu(packet, spiltBufs);
    if (spiltBufs.size() > 1) {
      for (auto& buf : spiltBufs) {
        processPacket(buf);
      }
    } else {
      processPacket(packet);
    }
  } else {
    processPacket(packet);
  }
  // 尽量处理完整的包,由decode根据自身需要决定是否拆包及annexb/avcc转换
  DecodeResult result = decode->decoderImp(packet);
  switch (result) {
    case DecodeResult::success:
      // input_image的数据添加进队列
      // EncoderInfo info = {};
      return WEBRTC_VIDEO_CODEC_OK;
    case DecodeResult::noConfig:
      return WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME;
    case DecodeResult::dataNoReady:
      return WEBRTC_VIDEO_CODEC_OK;
    case DecodeResult::dataError:
      return WEBRTC_VIDEO_CODEC_ERROR;
    case DecodeResult::timeout:
      return WEBRTC_VIDEO_CODEC_TIMEOUT;
    case DecodeResult::noSupport:
    case DecodeResult::noFind:
    case DecodeResult::openFailed:
    case DecodeResult::startFailed:
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    default:
      break;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

const char* RtcVideoDecoder::ImplementationName() const {
  return "avox webrtc decoder";
}

int32_t RtcVideoDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback_) {
  callback = callback_;
  return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoDecoder::DecoderInfo RtcVideoDecoder::GetDecoderInfo() const {
  webrtc::VideoDecoder::DecoderInfo info = {};
  info.implementation_name = ImplementationName();
  if (decode) {
    VCodecTh th = decode->getCodecTh();
    info.implementation_name = "webrtc " + codecDesc.name;
    info.is_hardware_accelerated = th != VCodecTh::other && th != VCodecTh::cpu;
  }
  return info;
}

bool RtcVideoDecoder::getFrameInfo(int64_t pts, uint32_t& rtp,
                                   std::optional<uint8_t>& qp) {
  std::lock_guard<std::mutex> lock(infoMutex);
  while (!imageTimeQueue.empty()) {
    const auto& front = imageTimeQueue.front();
    if (front.pts == pts) {
      rtp = front.rtpTimestamp;
      qp = front.qp;
      imageTimeQueue.pop_front();
      // LOGFLF(LogLevel::info, "get rtp:", rtp, " qp:", (int32_t)qp.value());
      return true;
    }
    if (front.pts < pts) {
      // 当前队列头的 pts 小于目标 pts，说明该记录已过期（可能发生了丢帧）
      imageTimeQueue.pop_front();
      // LOGFLF(LogLevel::info, "drop pts:", front.pts, " qp:",
      // front.qp.value());
      continue;
    }
    if (front.pts > pts) {
      // LOGFLF(LogLevel::info, "pts not found, target pts:", pts);
      // 队列头的 pts 已经超过了目标 pts，说明目标帧可能早已丢失或未记录
      break;
    }
  }
  return false;
}

void RtcVideoDecoder::onDecode(const YUVFrame& frame) {
  if (!callback) {
    return;
  }
  uint32_t rtpTimestamp = 0;
  std::optional<uint8_t> qp = std::nullopt;
  if (!getFrameInfo(frame.pts, rtpTimestamp, qp)) {
    // 没找到说明 pts 不匹配，根据 WebRTC 规范，通常使用 pts * 90 兜底
    rtpTimestamp = static_cast<uint32_t>(frame.pts * 90);
  }
  // log(LogLevel::info, "get cpu pts:", frame.pts, " rtpTimestamp:",
  // rtpTimestamp);
  webrtc::scoped_refptr<RtcVideoBuffer> videoBuffer =
      webrtc::make_ref_counted<RtcVideoBuffer>();
  videoBuffer->form(frame);
  webrtc::VideoFrame decodedFrame = webrtc::VideoFrame::Builder()
                                        .set_video_frame_buffer(videoBuffer)
                                        .set_timestamp_ms(frame.pts)
                                        .set_rtp_timestamp(rtpTimestamp)
                                        .build();
  callback->Decoded(decodedFrame, std::nullopt, qp);
}

void RtcVideoDecoder::onDecodeGpu(const GpuFrame& frame) {
  // log(LogLevel::info, "onDecode thread id:", std::this_thread::get_id());
  if (!callback) {
    return;
  }
  uint32_t rtpTimestamp = 0;
  std::optional<uint8_t> frame_qp = std::nullopt;
  if (!getFrameInfo(frame.pts, rtpTimestamp, frame_qp)) {
    rtpTimestamp = static_cast<uint32_t>(frame.pts * 90);
  }
  // log(LogLevel::info, "get gpu pts:", frame.pts, " rtpTimestamp:",
  // rtpTimestamp);
  webrtc::scoped_refptr<RtcVideoBuffer> videoBuffer =
      webrtc::make_ref_counted<RtcVideoBuffer>();
  videoBuffer->form(frame);
  webrtc::VideoFrame decoded_frame = webrtc::VideoFrame::Builder()
                                         .set_video_frame_buffer(videoBuffer)
                                         .set_timestamp_ms(frame.pts)
                                         .set_rtp_timestamp(rtpTimestamp)
                                         .build();
  callback->Decoded(decoded_frame, std::nullopt, frame_qp);
}

}
