#include "StreamRecorder.hpp"

#include <chrono>

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "TranscodeRecorder.hpp"

namespace avox {

StreamRecorder::StreamRecorder() {}

StreamRecorder::~StreamRecorder() { close(); }

void StreamRecorder::setRecState(RecorderState newState) {
  RecorderState oldState = state;
  state = newState;
  if (oldState != newState) {
    LOGFLF(LogLevel::info,
           "recorder state change:", getRecorderStateStr(oldState), " -> ",
           getRecorderStateStr(newState));
  }
  RDOB::dispatch(&IRecorderOb::onStateChange, oldState, state);
}

void StreamRecorder::setIoPlan(IoPlan plan) {
  ioPlan = plan;
  LOGFLF(LogLevel::info, "set io plan:", plan);
}

void StreamRecorder::setMuxerType(MuxerType type) {
  muxerType = type;
  LOGFLF(LogLevel::info, "set muxer type:", type);
}

void StreamRecorder::setVideoCodec(VCodecId codecId) {
  vcodecId = codecId;
  LOGFLF(LogLevel::info, "set video codec:", codecId,
         codecId == VCodecId::none ? " (discarded)" : "");
}

void StreamRecorder::setAudioCodec(ACodecId codecId) {
  aCodecId = codecId;
  LOGFLF(LogLevel::info, "set audio codec:", codecId,
         codecId == ACodecId::none ? " (discarded)" : "");
}

bool StreamRecorder::open(const char* url, const char* file) {
  if (state == RecorderState::opening || state == RecorderState::recording) {
    LOGFLF(LogLevel::info,
           "recorder is opening or recording, can't open again");
    return false;
  }
  inputUrl = url;
  outputFile = file;
  auto& ioSourceInfo = AvoxManager::Get().ioSources.initFunc(ioPlan);
  if (!ioSourceInfo.initFunc) {
    LOGFLF(LogLevel::error, "no io source for plan:", (int32_t)ioPlan);
    setRecState(RecorderState::completed);
    return false;
  }
  source.reset(ioSourceInfo.initFunc());
  source->addObserver(this);
  // 关联option(io.rtsp.speed/transport等,IO open前重放生效)
  source->linkOption(this);
  // 录制场景：speed>1时IO层非阻塞读取，尽快消费数据
  source->setFastRead(true);
  muxer.reset(new MediaMuxer());
  muxer->setMuxerType(muxerType);
  // open前设置编码: none=丢弃该轨(源层不解封装 + muxer层兜底过滤)
  muxer->setVideoCodec(vcodecId);
  muxer->setAudioCodec(aCodecId);
  bool bDiscardVideo = (vcodecId == VCodecId::none);
  bool bDiscardAudio = (aCodecId == ACodecId::none);
  source->disableVideo(bDiscardVideo);
  source->disableAudio(bDiscardAudio);
  if (!source->open(url)) {
    LOGFLF(LogLevel::error, "failed to open source:", url);
    setRecState(RecorderState::completed);
    return false;
  }
  setRecState(RecorderState::opening);
  LOGFLF(LogLevel::info, "recorder opening, input:", url, " output:", file);
  return true;
}

void StreamRecorder::close() {
  if (state == RecorderState::completed) {
    return;
  }
  setRecState(RecorderState::completed);
  if (source) {
    source->close();
    source.reset();
  }
  closeMuxer();
}

void StreamRecorder::closeMuxer() {
  if (!muxer) {
    return;
  }
  muxer->close();
  muxer.reset();
  LOGFLF(LogLevel::info, "recorder closed, output:", outputFile);
  RDOB::dispatch(&IRecorderOb::onComplete);
}

// IAVSourceOb::onReady - Track 信息准备好了
void StreamRecorder::onReady() {
  if (!source || !muxer) {
    return;
  }
  ISourceInfo* info = source->getSourceInfo();
  if (!info) {
    return;
  }
  LOGFLF(LogLevel::info, "source ready, video tracks:", info->videoSize(),
         " audio tracks:", info->audioSize());
  bHaveVideo = info->videoSize() > 0;
  muxer->open(outputFile.c_str());
  // 设置描述
  for (int32_t i = 0; i < info->videoSize(); i++) {
    VTrackDesc vdesc = info->getVideoDesc(i);
    muxer->setInVideoDesc(vdesc);
    LOGFLF(LogLevel::info, "set video desc:", vdesc);
  }
  for (int32_t i = 0; i < info->audioSize(); i++) {
    ATrackDesc adesc = info->getAudioDesc(i);
    muxer->setInAudioDesc(adesc);
    LOGFLF(LogLevel::info, "set audio desc:", adesc);
  }
  muxer->ready();
  setRecState(RecorderState::recording);
  progress.currentTimeMs = 0;
  progress.totalTimeMs = source->duration();
  LOGFLF(LogLevel::info,
         "recorder started recording, duration:", progress.totalTimeMs);
}

// IAVSourceOb::onPacket - 收到编码数据包
void StreamRecorder::onPacket(const AvoxPacket& packet) {
  if (muxer && state == RecorderState::recording) {
    muxer->pushPacket(packet);
  }
  // 按时间计算进度，用相对pts(减去baseTime)避免绝对时间戳超过duration
  if (!source) {
    return;
  }
  if (source->getBaseTime() != AVOX_NOVALID_PTS) {
    int64_t nowPts = source->getNowPts() - source->getBaseTime();
    if (nowPts != progress.currentTimeMs) {
      progress.currentTimeMs = nowPts;
      RDOB::dispatch(&IRecorderOb::onProgress, progress);
    }
  }
}

// IAVSourceOb::onClose - 源关闭
void StreamRecorder::onClose() { LOGFLF(LogLevel::info, "io source closed"); }

// IAVSourceOb::onComplete - 源播放完成
void StreamRecorder::onComplete() {
  LOGFLF(LogLevel::info, "io source completed");
  setRecState(RecorderState::completed);
  closeMuxer();
}

// IAVSourceOb::onError - 源错误
// AVSource 线程已停，可直接 close
void StreamRecorder::onError(AVError error, const char* msg) {
  LOGFLF(LogLevel::warn, "error:", (int32_t)error, " msg:", msg);
  // 录制中读到netTimeout = 对端断流且没有EOF, ffmpeg内部已等满自身重试+超时,
  // 转封装无法恢复连续性,已写部分就是完整产物,按正常完成收尾(onComplete由closeMuxer派发)
  bool bStreamEnd =
      (state == RecorderState::recording && error == AVError::netTimeout);
  setRecState(RecorderState::completed);
  if (!bStreamEnd) {
    RDOB::dispatch(&IRecorderOb::onIoError, error, msg);
  }
  closeMuxer();
}

IRecorder* createRecorder(bool bTranscode) {
  if (bTranscode) {
    return new TranscodeRecorder();
  }
  return new StreamRecorder();
}

void addRecorderOb(IRecorder* recorder, IRecorderOb* ob) {
  auto* sr = dynamic_cast<RDOB*>(recorder);
  if (sr) {
    sr->addObserver(ob);
  }
}

void removeRecorderOb(IRecorder* recorder, IRecorderOb* ob) {
  auto* sr = dynamic_cast<RDOB*>(recorder);
  if (sr) {
    sr->removeObserver(ob);
  }
}

}
