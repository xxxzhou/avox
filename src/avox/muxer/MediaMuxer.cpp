#include "MediaMuxer.hpp"

#include "../module/AvoxManager.hpp"
#include "../source/AVSource.hpp"

namespace avox {

MediaMuxer::MediaMuxer() { state = RecorderState::none; }

MediaMuxer::~MediaMuxer() { close(); }

void MediaMuxer::setRecState(RecorderState newState) {
  RecorderState oldState = state;
  state = newState;
  if (oldState != newState) {
    LOGFLF(LogLevel::info, "muxer state change:", getRecorderStateStr(oldState),
           " -> ", getRecorderStateStr(newState));
  }
  dispatch(&IRecorderOb::onStateChange, oldState, state);
}

void MediaMuxer::setMuxerType(MuxerType type) { muxerType = type; }

void MediaMuxer::setVideoCodec(VCodecId codecId) {
  vcodecId = codecId;
  bDisableVideo = (codecId == VCodecId::none);
  LOGFLF(LogLevel::info, "set video codec:", (int32_t)codecId,
         bDisableVideo ? " (disabled)" : "");
}

void MediaMuxer::setAudioCodec(ACodecId codecId) {
  bDisableAudio = (codecId == ACodecId::none);
  LOGFLF(LogLevel::info, "set audio codec:", (int32_t)codecId,
         bDisableAudio ? " (disabled)" : "");
}

bool MediaMuxer::open(const char* url_) {
  std::lock_guard<std::mutex> lock(ocMtx);
  if (ioMuxer) {
    LOGFLF(LogLevel::warn, "ioMuxer is not null");
    ioMuxer->close();
    ioMuxer.reset();
  }
  url = url_;
  bHaveAudio = false;
  bHaveVideo = false;
  bCheckAcc = false;
  bvcc = false;
  bStartPacket = false;
  if (muxerType == MuxerType::none) {
    muxerType = MuxerType::zlmediakit;
  }
  const auto& mutxerClass = AvoxManager::Get().muxers.initFunc(muxerType);
  ioMuxer = std::unique_ptr<IOMuxer>(mutxerClass.initFunc());
  LOGFLF(LogLevel::info, "url:", url, " mutxer type:", mutxerClass.desc.name);
  ioMuxer->setRawMuxer(this);
  ioMuxer->open(url_);
  // 给RawMuxer检测Onvif流信息
  onOpen();
  setRecState(RecorderState::opening);
  if (muxerOb) {
    muxerOb->onMuxerOpen(this);
  }
  return true;
}

void MediaMuxer::setInVideoDesc(const VTrackDesc& desc) {
  if (!ioMuxer) {
    LOGFLF(LogLevel::warn, "ioMuxer is null");
    return;
  }
  ioMuxer->setVideoDesc(desc);
  vcodecId = desc.codecId;
  bHaveVideo = true;
}

void MediaMuxer::setInAudioDesc(const ATrackDesc& desc) {
  if (!ioMuxer) {
    LOGFLF(LogLevel::warn, "ioMuxer is null");
    return;
  }
  ioMuxer->setAudioDesc(desc);
  bHaveAudio = true;
}

void MediaMuxer::ready() {
  // 在设置videodesc/audiodesc后调用,表示可以放入包了
  setRecState(RecorderState::recording);
}

void MediaMuxer::onProgress(int64_t ptsMs) {
  progress.currentTimeMs = ptsMs;
  // 节流:每100ms派发一次
  if (progress.currentTimeMs - lastProgressTimeMs >= 100) {
    lastProgressTimeMs = progress.currentTimeMs;
    dispatch(&IRecorderOb::onProgress, progress);
  }
}

void MediaMuxer::pushPacket(const AvoxPacket& packet) {
  // std::lock_guard<std::mutex> lock(ocMtx);
  // opening可能放入配置帧,也需要添加
  if (state != RecorderState::recording && state != RecorderState::opening) {
    LOGFLF(LogLevel::warn, "muxer is not recording");
    return;
  }
  PackType type = (PackType)packet.packtype;
  bool bAudio = (type == PackType::audio || type == PackType::aconfig);
  bool bVideo = (type == PackType::video || type == PackType::vconfig);
  // 关闭音频
  if (bDisableAudio && bAudio) {
    return;
  }
  // 关闭视频
  if (bDisableVideo && bVideo) {
    return;
  }
  if (bVideo) {
    if (!bCheckAcc) {
      bvcc = checkAvccPacket(packet.data.data, packet.data.size);
      bCheckAcc = true;
    }
    // 不同分包方式
    if (bvcc) {
      uint32_t naluLength = (packet.data.data[0] << 24) |
                            (packet.data.data[1] << 16) |
                            (packet.data.data[2] << 8) | packet.data.data[3];
      if (packet.data.size > naluLength) {
        splitAvccNalu(packet, spiltBufs);
      }
    } else {
      splitAnnexbNalu(packet, spiltBufs);
    }
    if (spiltBufs.size() > 1) {
      combineBufs.clear();
      for (auto& buf : spiltBufs) {
        // 根据测试,统一使用annexb格式推送到annexb/avcc源都行
        // 反之有机率不行
        if (bvcc) {
          avcc2AnnexbPacket(buf);
        }
        bool bNewFrame = naluNewFrame(vcodecId, buf.data.data + buf.prefixSize);
        if (bNewFrame) {
          combineBufs.push_back(buf);
        } else {
          if (combineBufs.size() > 0) {
            AvoxPacket& lastBuf = combineBufs.back();
            // 上面的spilt只是记录包指针偏移,这个buff是连续的
            lastBuf.data.size += buf.data.size;
          } else {
            // 放弃如NAL_SEI_PREFIX帧
            // LOGFLF(LogLevel::warn, "combine nalu type mismatch");
          }
        }
      }
      for (auto& buf : combineBufs) {
        singleVideo(buf);
      }
    } else {
      AvoxPacket tempData = packet;
      if (bvcc) {
        avcc2AnnexbPacket(tempData);
      }
      singleVideo(tempData);
    }
  } else {
    // 如果有视频,需要等待视频配置来了
    if (!bDisableVideo && (bHaveVideo && !bStartPacket)) {
      return;
    }
    ioMuxer->getPacketQueue().enqueueWait<AvoxPacket>(packet, copyBuf);
  }
}

void MediaMuxer::singleVideo(AvoxPacket& packet) {
  uint8_t nalu = getNalUnit(vcodecId, packet);
  bool bConfig = naluConfigFrame(vcodecId, nalu);
  bool bKey = naluKeyFrame(vcodecId, nalu);
  if (bConfig && !bStartPacket) {
    bStartPacket = true;
  }
  if (!bStartPacket) {
    return;
  }
  if (bConfig) {
    packet.packtype = (int32_t)PackType::vconfig;
  } else {
    packet.packtype = (int32_t)PackType::video;
  }
  packet.frameType = bKey ? 1 : 0;
  ioMuxer->getPacketQueue().enqueueWait<AvoxPacket>(packet, copyBuf);
}

void MediaMuxer::close() {
  std::lock_guard<std::mutex> lock(ocMtx);
  if (!ioMuxer) {
    return;
  }
  if (state == RecorderState::completed) {
    return;
  }
  onClose();
  if (ioMuxer) {
    ioMuxer->close();
    ioMuxer.reset();
  }
  if (muxerOb) {
    muxerOb->onMuxerClose();
  }
  LOGFLF(LogLevel::info, "url:", url);
  setRecState(RecorderState::completed);
}

void addMuxerOb(IMediaMuxer* muxer, IRecorderOb* ob) {
  MediaMuxer* mm = dynamic_cast<MediaMuxer*>(muxer);
  if (mm) {
    mm->addObserver(ob);
  } else {
    LOGFLF(LogLevel::warn, "muxer is not MediaMuxer");
  }
}

void removeMuxerOb(IMediaMuxer* muxer, IRecorderOb* ob) {
  MediaMuxer* mm = dynamic_cast<MediaMuxer*>(muxer);
  if (mm) {
    mm->removeObserver(ob);
  } else {
    LOGFLF(LogLevel::warn, "muxer is not MediaMuxer");
  }
}

}
