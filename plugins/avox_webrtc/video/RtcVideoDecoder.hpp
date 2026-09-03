#pragma once

#include <deque>

#include "../RtcHelper.hpp"
#include "api/video_codecs/bitstream_parser.h"
#include "api/video_codecs/video_decoder.h"
#include "avox/video/VideoDecoder.hpp"

namespace avox {

struct ImageTimeInfo {
  int64_t pts = 0;
  uint32_t rtpTimestamp = 0;
  // QP值
  std::optional<uint8_t> qp = std::nullopt;
};

// 把avox实现的编码封装成webrtc的接口
// 包含windows/ios/andorid相应的h264/h265的硬解/软解实现
// 相当于只来视频包的AVSource,需要分析包,配置帧用来初始化底层解码器
class RtcVideoDecoder : public webrtc::VideoDecoder,
                        public avox::IVideoDecoderOb {
 public:
  RtcVideoDecoder();
  virtual ~RtcVideoDecoder();

 protected:
  VCodecId codecId = VCodecId::none;
  std::unique_ptr<avox::VideoDecoder> decode = nullptr;
  webrtc::DecodedImageCallback* callback = nullptr;
  // sps,vps由avox本身分析,这里主要是为了QP(视频质量与压缩率)
  std::unique_ptr<webrtc::BitstreamParser> parser = nullptr;
  std::optional<int> qp = std::nullopt;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  // I帧/P帧合并包
  std::vector<AvoxPacket> combineBufs;  
  // std::map<int64_t, ImageTimeInfo> imageTimeMap;
  std::deque<ImageTimeInfo> imageTimeQueue; 
  std::mutex infoMutex;
  VCodecDesc codecDesc = {};
  // 配置帧检查是否有更新
  ConfigAddType configType = ConfigAddType::none;
  int64_t prePts = 0;

 public:
  bool getFrameInfo(int64_t pts, uint32_t& rtp, std::optional<uint8_t>& qp);
  // avox::VideoDecoder本身会根据需要来拆分转化annexb/avcc
  // 所以在这只需要处理配置帧更新逻辑,不需要折分组合
  // 1. 是否需要拆包检查是否有配置帧
  // 1. 检查是否配置帧,用于初始化
  // 2. 检查配置帧是否有更新,如果有更新,下个I帧前重置解码器
  void processPacket(AvoxPacket& packet);

 public:
  virtual bool Configure(const Settings& settings) override;
  virtual int32_t Release() override;
  virtual int32_t Decode(const webrtc::EncodedImage& input_image,
                         bool missing_frames,
                         int64_t render_time_ms = -1) override;
  virtual int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override;
  virtual DecoderInfo GetDecoderInfo() const override;
  virtual const char* ImplementationName() const override;

  // IVideoDecoderOb
 public:
  virtual void onDecode(const YUVFrame& frame) override;
  virtual void onDecodeGpu(const GpuFrame& frame) override;
};

}
