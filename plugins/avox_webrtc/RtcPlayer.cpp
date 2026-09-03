#include "RtcPlayer.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/player/MPCommand.hpp"

namespace avox {

RtcPlayer::RtcPlayer() {
  source = std::make_unique<RtcParse>();
  // 注册回调
  remoteVRender = std::make_unique<WindowRender>();
  mpCommands.setMaxSize(100);
  // 专门用来处理改变播放状态的线程
  startTask();
}

RtcPlayer::~RtcPlayer() {
  LOGFLF(LogLevel::info, "destroy rtc player");
  stopTask();
}

void RtcPlayer::setRollType(RtcRollType type) { source->setRollType(type); }

void RtcPlayer::addIceServer(const char* uri, const char* username,
                             const char* password) {
  source->addIceServer(uri, username, password);
}

void RtcPlayer::setSdpAgentOb(ISdpAgentOb* ob) { source->setSdpAgentOb(ob); }

void RtcPlayer::setVideoSource(IVideoSource* videoSource) {
  source->setVideoSource(videoSource);
}

void RtcPlayer::setAudioSource(IAudioSource* audioSource) {
  source->setAudioSource(audioSource);
}

bool RtcPlayer::open() {
  // 转到线程上执行
  auto openCmd = createCommand<MPCommandType::Open>("");
  mpCommands.enqueueWait(openCmd);
  return true;
}

void RtcPlayer::close() {
  // 需要把之前的命令清空吗？这样可以快速关闭
  mpCommands.clear();
  // 停止
  auto stopCmd = createCommand<MPCommandType::Close>();
  mpCommands.enqueueWait(stopCmd);
}

ISourceInfo* RtcPlayer::getRemoteSourceInfo() {
  return source->getSourceInfo();
}

ISourceInfo* RtcPlayer::getLocalSourceInfo() {
  // 返回本地源信息 (推流的信息)
  // 暂时返回 nullptr,后续可以根据需要实现
  return nullptr;
}

ISurfaceRender* RtcPlayer::getLocalSurfaceRender() {
  return source->getLocalSurfaceRender();
}

IAudioRender* RtcPlayer::getLocalAudioRender() {
  return source->getLocalAudioRender();
}

ISurfaceRender* RtcPlayer::getRemoteSurfaceRender() {
  return remoteVRender.get();
}

IAudioRender* RtcPlayer::getRemoteAudioRender() { return remoteARender.get(); }

const char* RtcPlayer::getLocalSdp() { return source->getLocalSdp(); }

void RtcPlayer::setRemoteSdp(const char* sdp) {
  // 转到播放线程上执行
  auto sdpCmd = createCommand<MPCommandType::SetRemoteSdp>(sdp);
  mpCommands.enqueueWait(sdpCmd);
  return;
}

void RtcPlayer::addIceCandidate(const char* candidate, const char* mid,
                                int mlineIndex) {
  source->addIceCandidate(candidate, mid, mlineIndex);
}

void RtcPlayer::onRunTask() {
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
        case MPCommandType::SetRemoteSdp: {
          cmdSetRemoteSdp(getCommand<MPCommandType::SetRemoteSdp>(cmd));
          break;
        }
        default:
          break;
      }
    }
    sleepTask(false, 10);
  }
}

void RtcPlayer::cmdOpen() {
  // 如果播放器已经打开,先清空资源
  if (state != PlayerState::none && state != PlayerState::stopped) {
    // 先关闭之前对象
    cmdClose();
  }
  remoteVRender->start();
  source->setObserver(this);
  bool bOpen = source->open();
  if (bOpen) {
    setState(PlayerState::opening);
  }
}

void RtcPlayer::cmdReady() {
  if (!source) {
    return;
  }
  auto vtracks = source->getVideoTracks();
  auto atracks = source->getAudioTracks();
  if (vtracks.size() > 0) {
    VideoDesc vdesc = vtracks[0].desc;
  }
  if (atracks.size() > 0) {
  }
  setState(PlayerState::ready);
  MPOB::dispatch(&IMediaPlayerOb::onReady);
}

void RtcPlayer::cmdSetRemoteSdp(SetRemoteSdpCommandPtr cmd) {
  const char* sdp = cmd->getData().c_str();
  source->setRemoteSdp(sdp);
}

void RtcPlayer::cmdClose() {
  source->close();
  source->setObserver(nullptr);
}

// IRawSourceOb 实现
void RtcPlayer::onReady() {
  auto readyCmd = createCommand<MPCommandType::Ready>();
  mpCommands.enqueueWait(readyCmd);
}

void RtcPlayer::onError(AVError error, const char* msg) {
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

void RtcPlayer::onVideoFrame(const YUVFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  // 收到远端 CPU 视频帧,渲染到远程渲染器
  remoteVRender->render(frame);
}

void RtcPlayer::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  // 收到远端 GPU 视频帧,渲染到远程渲染器
  remoteVRender->render(frame);
}

void RtcPlayer::onAudioFrame(const AvoxAFrame& frame, int32_t trackId) {
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
  }
  // 收到远端音频帧,播放到远程音频渲染器
  if (remoteARender) {
    remoteARender->render(frame.buffer);
  }
}

void RtcPlayer::onClose() { LOGFLF(LogLevel::info, "rtc closed"); }

}
