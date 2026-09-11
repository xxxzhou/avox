#include "TranscodeRecorder.hpp"

#include <chrono>

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../module/OptionKey.hpp"

namespace avox {

TranscodeRecorder::TranscodeRecorder() {
  surfaceRender = std::make_unique<SurfaceRenderVk>();
  audioRender = std::make_unique<AudioRender>();
  taskName = "transcode recorder";
  // 设定
  aCodecid = ACodecId::aac;
  outAudioDesc.channels = 2;
  outAudioDesc.format = AudioFormat::AVOX_AUDIO_S16;
  outAudioDesc.sampleRate = 32000;
}

// JsonOption - 有改动才同步成员,未设置的key走成员默认值
void TranscodeRecorder::onOptionChange(const char* key, ArgType option) {
  if (equalsIgnoreCase(key, AVOX_REC_HARD_DECODE_BOOL)) {
    bHardDecode = getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bHardDecode);
  } else if (equalsIgnoreCase(key, AVOX_REC_HARD_ENCODE_BOOL)) {
    bHardEncode = getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bHardEncode);
  }
}

TranscodeRecorder::~TranscodeRecorder() { close(); }

void TranscodeRecorder::setRecState(RecorderState newState) {
  RecorderState oldState = state;
  state = newState;
  if (oldState != newState) {
    LOGFLF(LogLevel::info,
           "recorder state change:", getRecorderStateStr(oldState), " -> ",
           getRecorderStateStr(newState));
  }
  RDOB::dispatch(&IRecorderOb::onStateChange, oldState, state);
}

void TranscodeRecorder::setIoPlan(IoPlan plan) {
  ioPlan = plan;
  LOGFLF(LogLevel::info, "set io plan:", plan);
}

void TranscodeRecorder::setMuxerType(MuxerType type) {
  muxerType = type;
  LOGFLF(LogLevel::info, "set muxer type:", type);
}

void TranscodeRecorder::setVideoCodec(VCodecId codecId) {
  vCodecId = codecId;
  LOGFLF(LogLevel::info, "set video codec:", codecId);
}

void TranscodeRecorder::setAudioCodec(ACodecId codecId) {
  aCodecid = codecId;
  LOGFLF(LogLevel::info, "set audio codec:", codecId);
}

void TranscodeRecorder::setVideoDesc(const VideoDesc& desc) {
  outVideoDesc = desc;
  bSetOutVideo = true;
}

void TranscodeRecorder::setAudioDesc(const AudioDesc& desc) {
  outAudioDesc = desc;
  bSetOutAudio = true;
}

ISurfaceRender* TranscodeRecorder::getSurfaceRender() {
  return surfaceRender.get();
}

IAudioRender* TranscodeRecorder::getAudioRender() { return audioRender.get(); }

bool TranscodeRecorder::open(const char* url, const char* file) {
  if (state == RecorderState::opening || state == RecorderState::recording) {
    LOGFLF(LogLevel::info,
           "recorder is opening or recording, can't open again");
    return false;
  }
  inputUrl = url;
  outputFile = file ? file : "";
  bNoOutput = outputFile.empty();
  // hard开关经onOptionChange在set时同步, 未设置保持默认true(平台原生硬编)
  LOGFLF(LogLevel::info, "rec.hard.decode:", bHardDecode,
         " rec.hard.encode:", bHardEncode);
  // 解码源
  source = std::make_unique<AMediaSource>();
  source->setUri(url);
  // IO方案与上层选项链: io.rtsp.speed等经OptionLink链到内部ioSource
  source->setIoPlan(ioPlan);
  source->linkOption(this);
  source->setHardDecode(bHardDecode);
  // open前设置编码: none=丢弃该轨(AMediaSource层连对应解码器都不开)
  // 空输出(转码直出)不建 muxer,帧经 ISurfaceRender/IAudioRender 对外
  if (!bNoOutput) {
    muxer = std::make_unique<RawMuxer>();
    muxer->setMuxerType(muxerType);
    muxer->setHardEncode(bHardEncode);
    muxer->setVideoCodec(vCodecId);
    muxer->setAudioCodec(aCodecid);
  }
  bool bDiscardVideo = (vCodecId == VCodecId::none);
  bool bDiscardAudio = (aCodecid == ACodecId::none);
  source->disableVideo(bDiscardVideo);
  source->disableAudio(bDiscardAudio);
  source->addObserver(this);
  // 启动编码线程
  startTask();
  // 打开源
  if (!source->open()) {
    LOGFLF(LogLevel::error, "failed to open source:", url);
    stopTask();
    setRecState(RecorderState::failed);
    return false;
  }
  ioSource = source->getSource();
  setRecState(RecorderState::opening);
  LOGFLF(LogLevel::info, "transcode recorder opening, input:", url,
         " output:", file);
  return true;
}

void TranscodeRecorder::close() {
  // 先置completed: 封死close期间seek/getSourceInfo重入ioSource的窗口
  // (编码线程在join前会source.reset(), ioSource随后悬空)
  // 失败态保持failed, 避免close把失败洗成完成
  if (state != RecorderState::failed) {
    setRecState(RecorderState::completed);
  }
  if (audioRender) {
    // 空输出音频阻塞反压,closeTap 唤醒可能阻塞在 push 的解码线程
    if (bNoOutput) {
      audioRender->closeTap();
    }
    audioRender->close();
  }
  // 停止编码线程(编码线程排空队列并关源后join返回)
  stopTask();
  LOGFLF(LogLevel::info, "encode thread stopped, recorder close done");
}

// IRawSourceOb::onReady - 解码就绪
void TranscodeRecorder::onReady() {
  if (!source) {
    return;
  }
  const auto& vTracks = source->getVideoTracks();
  const auto& aTracks = source->getAudioTracks();
  // 打开muxer(空输出不建 muxer)
  if (muxer) {
    muxer->open(outputFile.c_str());
  }
  // 设置
  if (!vTracks.empty() && surfaceRender) {
    VTrackDesc vdesc = vTracks[0];
    // 录制颜色空间: shader 矩阵与 encoder tag 的共同源(阶段3 量程分流在此切换)
    ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
    vdesc.desc.colorSpace = cs;
    // 同源驱动 shader 矩阵(rgba2YUV)
    surfaceRender->setColorSpace(cs);
    if (muxer) {
      muxer->setVideoCodec(vCodecId);
    }
    // 硬编给NV12,软编给YUV420P;空输出(无编码)用YUV420P对外
    if (bHardEncode && muxer) {
      surfaceRender->setOffSurface(YuvType::nv12);
      vdesc.desc.type = YuvType::nv12;
    } else {
      surfaceRender->setOffSurface(YuvType::yuv420P);
      vdesc.desc.type = YuvType::yuv420P;
    }
    if (bSetOutVideo) {
      if (outVideoDesc.width > 0 && outVideoDesc.height > 0 &&
          (outVideoDesc.width != vdesc.desc.width ||
           outVideoDesc.height != vdesc.desc.height)) {
        surfaceRender->enableSizeChange(outVideoDesc.width,
                                        outVideoDesc.height);
        vdesc.desc.width = outVideoDesc.width;
        vdesc.desc.height = outVideoDesc.height;
      }
    }
    if (muxer) {
      muxer->setInVideoDesc(vdesc);
    }
    LOGFLF(LogLevel::info, "set video desc:", vdesc);
  }
  if (!aTracks.empty()) {
    if (muxer) {
      muxer->setAudioCodec(aCodecid);
      // 音频是否重采样
      if (bSetOutAudio && outAudioDesc.bValid()) {
        muxer->setAudioDesc(outAudioDesc);
      }
      muxer->setInAudioDesc(aTracks[0]);
    }
    // AudioRender setDesc(源格式),tap 可在之后 open
    audioRender->setDesc(aTracks[0].desc);
    // 空输出且无视频轨:音频阻塞反压;有视频时由视频 onFrame 主导反压到 IO
    if (bNoOutput && vTracks.empty()) {
      audioRender->setTapBlock(true);
    }
    LOGFLF(LogLevel::info, "set audio desc:", aTracks[0]);
  }
  if (muxer) {
    muxer->ready();
  }
  setRecState(RecorderState::recording);
  // 初始化进度
  progress.currentTimeMs = 0;
  progress.totalTimeMs = ioSource->duration();
  LOGFLF(LogLevel::info, "transcode recorder started recording, duration:",
         progress.totalTimeMs, "ms");
}

// IRawSourceOb - 解码回调
// 在此线程做GPU处理(renderFrame)与unpack(getCpuFrame),
// 处理后的YUVFrame拷贝入队,避免GPU共享buffer被下一帧覆盖
void TranscodeRecorder::onVideoFrame(const YUVFrame& frame, int32_t trackId) {
  // seek 期间丢弃帧(含 flushDecoders 同步重入的尾帧),避免旧帧污染输出
  // close(stopTask)后丢弃: 停掉无谓的GPU处理,编码线程排空后即关源
  if (bSeeking.load() || !running()) {
    return;
  }
  // 交给vulkan处理,render末尾自动dispatch onFrame给外部(离屏bCpuOut)
  surfaceRender->render(frame);
  // 空输出:仅对外回调,不入编码队列
  if (bNoOutput) {
    return;
  }
  // 得到处理后的帧,入队给编码线程
  YUVFrame yframe = {};
  if (!surfaceRender->getCpuFrame(yframe)) {
    return;
  }
  vFrameQueue.enqueueWait<YUVFrame>(yframe, copyBufHost);
  // LOGFLF(LogLevel::info, "in video framesize:", videoQueue.size(),
  //        " frame:", yframe);
}

void TranscodeRecorder::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
  if (bSeeking.load() || !running()) {
    return;
  }
  // 空输出:交给vulkan处理后对外(render自动dispatch onFrame),不入编码队列
  if (bNoOutput) {
    surfaceRender->render(frame);
    return;
  }
  vFrameQueue.enqueueWait<GpuFrame>(frame, copyBufGpu);
}

void TranscodeRecorder::onAudioFrame(const AvoxAFrame& frame, int32_t trackId) {
  if (bSeeking.load() || !running()) {
    return;
  }
  // 推给 AudioRender(供 tap 读取,onRender 默认空体不发声)
  audioRender->render(frame.buffer, frame.pts);
  // 空输出:仅对外回调,不入编码队列
  if (bNoOutput) {
    return;
  }
  aFrameQueue.enqueueWait<AvoxAFrame>(frame, copyAudioBuf);
  // LOGFLF(LogLevel::info, "in audio framesize:", audioQueue.size(),
  //        " pts:", frame.pts, " size:", frame.buffer.size);
}

void TranscodeRecorder::onClose() {
  LOGFLF(LogLevel::info, "io source completed");
}

void TranscodeRecorder::onError(AVError error, const char* msg) {
  LOGFLF(LogLevel::warn, "error:", (int32_t)error, " msg:", msg);
  // 编码线程ioComplete分支已收尾(完成/失败), 迟到的错误不再处理
  if (state == RecorderState::completed || state == RecorderState::failed) {
    return;
  }
  // 录制中读到netTimeout/eof = 源自然结束(对端断流无EOF/服务端正常BYE),
  // 按完成收尾: 编码线程耗尽队列后正常关muxer并回调onComplete
  if (state == RecorderState::recording) {
    bool bStreamEnd =
        (error == AVError::netTimeout || error == AVError::endOfFile);
    setRecState(RecorderState::completed);
    if (!bStreamEnd) {
      RDOB::dispatch(&IRecorderOb::onIoError, error, msg);
    }
    return;
  }
  // opening态(一帧未写)被源终止 = 没拿到任何数据(如平台拒回放会话),
  // 属失败而非完成: 标failed只报onIoError, 收尾不发onComplete, 避免上层把失败当成功
  setRecState(RecorderState::failed);
  RDOB::dispatch(&IRecorderOb::onIoError, error, msg);
}

// RunTask - 编码线程
void TranscodeRecorder::onRunTask() {
  while (running() || !vFrameQueue.empty() || !aFrameQueue.empty()) {
    // seek 必须在编码线程处理(与队列/muxer 同属一线程,避免与 IO 线程竞争)
    if (bSeekPending.load()) {
      bSeekPending.store(false);
      doSeek();
    }
    VideoFramePtr vframe;
    if (vFrameQueue.dequeue(vframe)) {
      processVideo(vframe);
    }
    AudioFramePtr aframe;
    if (aFrameQueue.dequeue(aframe)) {
      processAudio(aframe);
    }
    // 如果io源结束了,并且队列全空了，则关闭编码器
    if (source && source->ioComplete() && vFrameQueue.empty() &&
        aFrameQueue.empty()) {
      // recording按完成收尾; opening/failed态保持, 等onError定failed(不发onComplete)
      if (state == RecorderState::recording) {
        setRecState(RecorderState::completed);
      }
      break;
    }
    // 关闭源,关闭解码
    if (!running() && source) {
      source->close();
      source.reset();
    }
    sleepTask(true);
  }
  if (source) {
    source->close();
    source.reset();
  }
  if (muxer) {
    muxer->close();
    muxer.reset();
  }
  LOGFLF(LogLevel::info, "transcode recorder closed, output:", outputFile);
  // 失败态不回调onComplete(已报onIoError), 避免上层把失败当完成
  if (state == RecorderState::completed) {
    RDOB::dispatch(&IRecorderOb::onComplete);
  }
}

void TranscodeRecorder::processVideo(VideoFramePtr vframe) {
  if (!vframe || !vframe->buffer || !muxer || !running()) {
    return;
  }
  // VideoFrame里的SwVideoBuffer恒为packed布局,to()取split帧给编码器,
  // 需要重排时数据拷进splitBuffer(buffer可能与渲染线程共享,不可原地改)
  std::shared_ptr<SwVideoBuffer> hostBuffer =
      std::static_pointer_cast<SwVideoBuffer>(vframe->buffer);
  YUVFrame yframe = {};
  if (!splitBuffer) {
    splitBuffer = std::make_unique<ImageBuffer>();
  }
  if (!hostBuffer->to(yframe, splitBuffer.get())) {
    return;
  }
  yframe.pts = vframe->pts;
  yframe.dts = vframe->dts;
  muxer->pushFrame(yframe);
  // 更新进度
  updateProgress();
}

void TranscodeRecorder::processAudio(AudioFramePtr aframe) {
  if (!aframe || !muxer || !running()) {
    return;
  }
  if (muxer->getState() != RecorderState::recording) {
    return;
  }
  AvoxAFrame aframeOut = {};
  aframeOut.pts = aframe->getPts();
  aframeOut.buffer.data = aframe->point();
  aframeOut.buffer.size = aframe->getSize();
  muxer->pushFrame(aframeOut);
  updateProgress();
}

void TranscodeRecorder::updateProgress() {
  if (!ioSource) {
    return;
  }
  int64_t baseTime = ioSource->getBaseTime();
  if (baseTime != AVOX_NOVALID_PTS) {
    int64_t nowPts = ioSource->getNowPts() - baseTime;
    if (nowPts != progress.currentTimeMs) {
      progress.currentTimeMs = nowPts;
      RDOB::dispatch(&IRecorderOb::onProgress, progress);
    }
  }
}

bool TranscodeRecorder::seek(int64_t posMs) {
  if (state != RecorderState::recording) {
    LOGFLF(LogLevel::warn, "seek ignored: not recording");
    return false;
  }
  if (!ioSource) {
    return false;
  }
  // 直播/未知时长 或 不可 seek 直接拒绝
  int64_t duration = ioSource->duration();
  if (duration <= 0) {
    LOGFLF(LogLevel::warn, "seek ignored: unknown duration(live?)");
    return false;
  }
  if (!ioSource->canSeek()) {
    LOGFLF(LogLevel::warn, "seek ignored: source not seekable");
    return false;
  }
  int64_t baseTime = ioSource->getBaseTime();
  if (baseTime == AVOX_NOVALID_PTS) {
    LOGFLF(LogLevel::warn, "seek ignored: baseTime not ready");
    return false;
  }
  // 对外相对坐标 clamp 到 [0, duration],内部转绝对 PTS
  if (posMs < 0) {
    posMs = 0;
  }
  if (posMs > duration) {
    posMs = duration;
  }
  seekTargetMs.store(posMs + baseTime);
  bSeekPending.store(true);
  LOGFLF(LogLevel::info, "seek to:", posMs, "ms (abs:", posMs + baseTime, ")");
  return true;
}

int64_t TranscodeRecorder::getDuration() {
  if (state != RecorderState::recording) {
    return 0;
  }
  return progress.totalTimeMs > 0 ? progress.totalTimeMs : 0;
}

ISourceInfo* TranscodeRecorder::getSourceInfo() {
  if (state != RecorderState::recording) {
    return nullptr;
  }
  return ioSource;
}

// 编码线程执行:关闭队列→清帧→flush 解码/编码→seekTo(内含
// pause/resume)→清帧→恢复队列 不用 ioSource->pause(true): 会停 IO
// 线程,对部分源(RTSP)导致 seek 后收不到数据 用 setClose(true) 关闭队列:
// enqueueWait 不阻塞直接返回,dequeue 返回 false TranscodeRecorder seek
// 仅对本地文件(duration>0)生效,seekTo 内部自管 pause/resume
void TranscodeRecorder::doSeek() {
  if (state != RecorderState::recording || !ioSource || !source) {
    return;
  }
  // bSeeking:跳过回调中的 GPU/音频处理(含 flushDecoders 同步重入的尾帧)
  bSeeking.store(true);
  // 关闭队列:解码线程 enqueueWait 不阻塞直接返回,编码线程 dequeue 返回 false
  vFrameQueue.setClose(true);
  aFrameQueue.setClose(true);
  // preSeek:置 flush+discard flag,IO 线程在 onPacket 中执行 flush 并丢弃后续包
  source->preSeek();
  // 清空残留帧,定位 IO
  vFrameQueue.clear();
  aFrameQueue.clear();
  // seek:调 io seekTo 并解除 discard,IO 线程恢复处理包
  source->seek(seekTargetMs.load());
  // 恢复队列:后续帧正常入队/出队
  vFrameQueue.setClose(false);
  aFrameQueue.setClose(false);
  bSeeking.store(false);
  LOGFLF(LogLevel::info, "seek done, abs pts:", seekTargetMs.load());
}

}
