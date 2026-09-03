#include "SourcePlayer.hpp"

#include "../module/AvoxManager.hpp"

namespace avox {

SourcePlayer::SourcePlayer() {
  windowRender = std::make_unique<WindowRender>();
  audioRender = std::unique_ptr<AudioOutput>(getDefaultAudioOutput());
  rawMuxer = std::make_unique<RawMuxer>();
  source = std::make_unique<DeviceSource>();
  subtitleView = std::make_unique<SubtitleView>();
  subtitleView->setAsrMode(AsrMode::streaming);
  audioRender->enableOutput(false);
  mpCommands.setMaxSize(100);
  // 专门用来处理改变播放状态的线程
  startTask();
}

SourcePlayer::~SourcePlayer() {
  LOGFLF(LogLevel::info, "destroy source player");
  stopTask();
}

ISurfaceRender* SourcePlayer::getSurfaceRender() { return windowRender.get(); }

IAudioRender* SourcePlayer::getAudioRender() { return audioRender.get(); }

ISubtitle* SourcePlayer::getSubtitle() { return subtitleView.get(); }

void SourcePlayer::setAudioSource(IAudioSource* device) {
  source->setAudioSource(device);
}

void SourcePlayer::setVideoSource(IVideoSource* device) {
  source->setVideoSource(device);
}

IMediaMuxer* SourcePlayer::getMuxer() { return rawMuxer.get(); }

bool SourcePlayer::open() {
  // rawMuxer在源可用后,才能录制
  rawMuxer->setContext(this);
  // 转到线程上执行
  auto openCmd = createCommand<MPCommandType::Open>("");
  mpCommands.enqueueWait(openCmd);
  return true;
}

void SourcePlayer::close() {
  // 需要把之前的命令清空吗？这样可以快速关闭
  mpCommands.clear();
  // 停止
  auto stopCmd = createCommand<MPCommandType::Close>();
  mpCommands.enqueueWait(stopCmd);
}

PlayerState SourcePlayer::getState() { return state; }

void SourcePlayer::onRunTask() {
  while (running()) {
    MPCommandPtr cmd = nullptr;
    if (mpCommands.dequeue(cmd)) {
      switch (cmd->type) {
        case MPCommandType::Open: {
          // open,得到IO回复变成ready
          cmdOpen();
          break;
        }
        case MPCommandType::Ready: {
          // MediaPlayer::onOpen后得到流信息
          // 在打开解码线程后，进入opening状态
          cmdReady();
          break;
        }
        case MPCommandType::Close: {
          // stop
          cmdClose();
          break;
        }
        default:
          break;
      }
    }
    sleepTask(false, 10);
  }
  if (source) {
    source->close();
    source->removeObserver(this);
  }
}

void SourcePlayer::cmdOpen() {
  if (!source) {
    LOGFLF(LogLevel::warn, "source is null");
    return;
  }
  // 如果播放器已经打开,先清空资源
  if (state != PlayerState::none && state != PlayerState::stopped) {
    // 先关闭之前对象
    cmdClose();
  }
  source->setObserver(this);
  windowRender->addObserver(this);
  windowRender->start();
  bool bOpen = source->open();
  if (bOpen) {
    setState(PlayerState::opening);
  }
}

void SourcePlayer::cmdReady() {
  if (!source) {
    return;
  }
  auto vtracks = source->getVideoTracks();
  auto atracks = source->getAudioTracks();
  if (vtracks.size() > 0) {
    VideoDesc vdesc = vtracks[0].desc;
    LOGFLF(LogLevel::info, "video track: ", vdesc);
    subtitleView->setWindowRender(windowRender.get());
  }
  if (atracks.size() > 0) {
    AudioDesc adesc = atracks[0].desc;
    LOGFLF(LogLevel::info, "audio track: ", adesc);
    audioRender->setDesc(adesc);
    subtitleView->setAudioDesc(adesc);
    bAudioRender = true;
  }
  setState(PlayerState::ready);
  MPOB::dispatch(&IMediaPlayerOb::onReady);
}

void SourcePlayer::cmdClose() {
  if (source) {
    source->close();
    source->removeObserver(this);
  }
  // 如果先关渲染再关资源时有可能导致android surface出错
  // 这样下次android相机打开可能就不能正常显示
  windowRender->stop();
  windowRender->removeObserver(this);
  subtitleView->close();
  setState(PlayerState::stopped);
  MPOB::dispatch(&IMediaPlayerOb::onClose);
}

ISourceInfo* SourcePlayer::getSourceInfo() { return source.get(); }

void SourcePlayer::onMuxerOpen(MediaMuxer* muxer) {
  auto vtracks = source->getVideoTracks();
  auto atracks = source->getAudioTracks();
  if (vtracks.size() > 0) {
    bool bHard = rawMuxer->getHardEncode();
    YuvType outYuv = windowRender->getOutYuv();
    // 如果自身输出YUV420P,关闭硬解
    if (outYuv == YuvType::yuv420P) {
      bHard = false;
      rawMuxer->setHardEncode(false);
    }
    VTrackDesc vdesc = vtracks[0];
    // 录制颜色空间: shader 矩阵与 encoder tag 的共同源(阶段3 量程分流在此切换)
    ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
    vdesc.desc.colorSpace = cs;
    // 同源驱动 shader 矩阵(rgba2YUV)
    windowRender->setColorSpace(cs);
    if (bHard) {
#ifdef WIN32
      // 如果没打开YUV输出
      if (outYuv == YuvType::other) {
        windowRender->enableYuvOut(YuvType::nv12);
        bMuxerCpu = true;
      }
#endif
      vdesc.desc.type = YuvType::nv12;
    } else {
      // ffmpeg软解需要Yuv420P
      windowRender->enableYuvOut(YuvType::yuv420P);
      vdesc.desc.type = YuvType::yuv420P;
    }
    muxer->setInVideoDesc(vdesc);
  }
  if (atracks.size() > 0) {
    muxer->setInAudioDesc(atracks[0]);
  }
  muxer->ready();
}

void SourcePlayer::onMuxerClose() {
  // 如果是因为录制打开的CPU模式，现在需要关闭
  if (bMuxerCpu) {
    windowRender->disableYuvOut();
    bMuxerCpu = false;
  }
}

void SourcePlayer::onReady() {
  auto readyCmd = createCommand<MPCommandType::Ready>();
  mpCommands.enqueueWait(readyCmd);
}

void SourcePlayer::onError(AVError error, const char* msg) {
  if (error == AVError::none) {
    return;
  }
  if (error == AVError::endOfFile) {
    setState(PlayerState::completed);
    MPOB::dispatch(&IMediaPlayerOb::onComplete);
  } else {
    if (error == AVError::deviceDisable) {
    }
    LOGFLF(LogLevel::warn, "raw source error:", getAVErrorStr(error),
           " msg:", msg);
    MPOB::dispatch(&IMediaPlayerOb::onIoError, error, msg);
  }
}

void SourcePlayer::onVideoFrame(const YUVFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  windowRender->render(frame);
  if (rawMuxer->getState() == RecorderState::recording) {
    windowRender->pushFrame(rawMuxer.get());
  }
}

void SourcePlayer::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  // HighClock clock = {};
  windowRender->render(frame);
  if (rawMuxer->getState() == RecorderState::recording) {
    windowRender->pushFrame(rawMuxer.get());
  }
}

void SourcePlayer::onAudioFrame(const AvoxAFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  if (audioRender) {
    audioRender->render(frame.buffer, frame.pts);
  }
  if (subtitleView) {
    subtitleView->inputSpeech(frame.buffer, frame.pts);
  }
  if (rawMuxer->getState() == RecorderState::recording) {
    rawMuxer->pushFrame(frame);
  }
}

void SourcePlayer::onClose() { subtitleView->close(); }

}
