#include "RtcPlayer.hpp"

#include "avox/audio/AudioOutput.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/player/MPCommand.hpp"

namespace avox {

RtcPlayer::RtcPlayer() {
  source = std::make_unique<RtcParse>();
  // 注册回调
  remoteVRender = std::make_unique<WindowRender>();
  // 远端音频接平台设备输出(裸AudioRender不发声)
  remoteARender.reset(getDefaultAudioOutput());
  localSourceInfo = std::make_unique<RtcSourceInfo>();
  mpCommands.setMaxSize(100);
  // 专门用来处理改变播放状态的线程
  startTask();
}

RtcPlayer::~RtcPlayer() {
  LOGFLF(LogLevel::info, "destroy rtc player");
  stopTask();
}

VTrackDesc RtcSourceInfo::getVideoDesc(int32_t index) {
  if (index < 0 || index >= (int32_t)vtracks.size()) {
    return {};
  }
  return vtracks[index];
}

ATrackDesc RtcSourceInfo::getAudioDesc(int32_t index) {
  if (index < 0 || index >= (int32_t)atracks.size()) {
    return {};
  }
  return atracks[index];
}

void RtcPlayer::setRollType(RtcRollType type) { source->setRollType(type); }

void RtcPlayer::addIceServer(const char* uri, const char* username,
                             const char* password) {
  source->addIceServer(uri, username, password);
}

void RtcPlayer::setVideoDirection(RtpDirection direction) {
  source->setVideoDirection(direction);
}

void RtcPlayer::setAudioDirection(RtpDirection direction) {
  source->setAudioDirection(direction);
}

void RtcPlayer::setSendVideoBitrate(int32_t maxKbps) {
  source->setSendVideoBitrate(maxKbps);
}

void RtcPlayer::setSendVideoFps(int32_t maxFps) {
  source->setSendVideoFps(maxFps);
}

void RtcPlayer::setPreferredVideoCodec(const char* codec) {
  source->setPreferredVideoCodec(codec);
}

void RtcPlayer::setEnableDataChannel(bool bEnable) {
  source->setEnableDataChannel(bEnable);
}

void RtcPlayer::setSdpAgentOb(ISdpAgentOb* ob) {
  userSdpOb = ob;
  // open已生成localSdp时立即回放
  if (ob) {
    const char* sdp = source->getLocalSdp();
    if (sdp && sdp[0] != '\0') {
      ob->onLocalSdp(sdp);
    }
  }
}

void RtcPlayer::setSignalChannel(ISignalChannel* channel) {
  signalChannel = channel;
}

void RtcPlayer::setVideoSource(IVideoSource* videoSource) {
  source->setVideoSource(videoSource);
}

void RtcPlayer::setAudioSource(IAudioSource* audioSource) {
  source->setAudioSource(audioSource);
}

void RtcPlayer::setAutoReconnect(bool bEnable, int32_t maxRetries) {
  bAutoReconnect = bEnable;
  maxRetryCount = maxRetries;
}

bool RtcPlayer::open() {
  // 转到线程上执行, 回填真实结果
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();
  bOpenRequested = true;
  retryCount = 0;
  {
    std::lock_guard<std::mutex> lock(cmdMtx);
    openPromise = promise;
  }
  auto openCmd = createCommand<MPCommandType::Open>("");
  mpCommands.enqueueWait(openCmd);
  // 超时防死锁(线程已停等异常场景)
  if (future.wait_for(std::chrono::seconds(3)) == std::future_status::ready) {
    return future.get();
  }
  return false;
}

void RtcPlayer::close() {
  bOpenRequested = false;
  // 需要把之前的命令清空，这样可以快速关闭
  mpCommands.clear();
  // 停止
  auto stopCmd = createCommand<MPCommandType::Close>();
  mpCommands.enqueueWait(stopCmd);
}

void RtcPlayer::reconnect() {
  if (!bOpenRequested) {
    LOGFLF(LogLevel::warn, "reconnect need open first");
    return;
  }
  retryCount = 0;
  auto openCmd = createCommand<MPCommandType::Open>("");
  mpCommands.enqueueWait(openCmd);
}

ISourceInfo* RtcPlayer::getRemoteSourceInfo() {
  return source->getSourceInfo();
}

ISourceInfo* RtcPlayer::getLocalSourceInfo() {
  localSourceInfo->vtracks.clear();
  localSourceInfo->atracks.clear();
  VTrackDesc vdesc = {};
  vdesc.desc = source->getLocalVideoDesc();
  if (vdesc.desc.width > 0) {
    localSourceInfo->vtracks.push_back(vdesc);
  }
  ATrackDesc adesc = {};
  adesc.desc = source->getLocalAudioDesc();
  if (adesc.desc.sampleRate > 0) {
    localSourceInfo->atracks.push_back(adesc);
  }
  return localSourceInfo.get();
}

PlayerState RtcPlayer::getState() { return state; }

RtcConnState RtcPlayer::getConnectionState() { return source->getConnState(); }

double RtcPlayer::getFps() {
  return (double)fpsValue.load(std::memory_order_acquire);
}

float RtcPlayer::getLossRate() { return source->getLossRate(); }

int32_t RtcPlayer::getRttMs() { return source->getRttMs(); }

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

bool RtcPlayer::sendDataChannel(const char* data, int32_t size) {
  return source->sendDataChannel(data, size);
}

void RtcPlayer::addOb(IRtcPlayerOb* ob) {
  if (ob) {
    Observer<IRtcPlayerOb>::addObserver(ob);
  }
}

void RtcPlayer::removeOb(IRtcPlayerOb* ob) {
  Observer<IRtcPlayerOb>::removeObserver(ob);
}

const char* RtcPlayer::getLocalSdp() { return source->getLocalSdp(); }

void RtcPlayer::setRemoteSdp(const char* sdp) {
  // 转到播放线程上执行
  if (!sdp) {
    return;
  }
  auto sdpCmd = createCommand<MPCommandType::SetRemoteSdp>(sdp);
  mpCommands.enqueueWait(sdpCmd);
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
    pollConnection();
    sleepTask(false, 10);
  }
}

void RtcPlayer::cmdOpen() {
  // 如果播放器已经打开,先清空资源
  if (state != PlayerState::none && state != PlayerState::stopped) {
    // 先关闭之前对象
    cmdClose();
  }
  bFirstFrame = false;
  bAudioDescSet = false;
  remoteVRender->start();
  source->setObserver(this);
  // 本地SDP/ICE与DataChannel消息经此中转
  source->setSdpAgentOb(this);
  source->setDataChannelMsgCb([this](const char* data, int32_t size) {
    Observer<IRtcPlayerOb>::dispatch(&IRtcPlayerOb::onDataChannelMsg, data,
                                     size);
  });
  bool bOpen = source->open();
  if (bOpen) {
    setState(PlayerState::opening);
    // 信令通道: 本地SDP/ICE自动送出, 远端消息回填
    if (signalChannel) {
      signalChannel->setSignalOb(this);
      bOpen = signalChannel->connect();
      if (!bOpen) {
        LOGFLF(LogLevel::warn, "signal channel connect failed");
      }
    }
  } else {
    LOGFLF(LogLevel::warn, "rtc source open failed");
    MPOB::dispatch(&IMediaPlayerOb::onIoError, AVError::urlNoSupport,
                   "rtc open failed");
  }
  // 回填open结果
  std::shared_ptr<std::promise<bool>> promise;
  {
    std::lock_guard<std::mutex> lock(cmdMtx);
    promise = openPromise;
    openPromise.reset();
  }
  if (promise) {
    promise->set_value(bOpen);
  }
}

void RtcPlayer::cmdReady() {
  if (!source) {
    return;
  }
  setState(PlayerState::ready);
  MPOB::dispatch(&IMediaPlayerOb::onReady);
}

void RtcPlayer::cmdSetRemoteSdp(SetRemoteSdpCommandPtr cmd) {
  const char* sdp = cmd->getData().c_str();
  source->setRemoteSdp(sdp);
}

void RtcPlayer::cmdClose() {
  // bOpenRequested保留: cmdOpen内部重开也走这里, 不能清掉用户的open意图
  retryAtMs = 0;
  if (signalChannel) {
    signalChannel->close();
  }
  if (remoteARender) {
    remoteARender->close();
  }
  source->close();
  source->setObserver(nullptr);
  bAudioDescSet = false;
}

void RtcPlayer::cmdReopen() {
  cmdClose();
  cmdOpen();
}

void RtcPlayer::pollConnection() {
  RtcConnState cs = source->getConnState();
  if (cs != lastConnState.load(std::memory_order_acquire)) {
    lastConnState.store(cs, std::memory_order_release);
    Observer<IRtcPlayerOb>::dispatch(&IRtcPlayerOb::onConnectionState, cs);
  }
  if (cs == RtcConnState::connected) {
    // 连上后重试计数清零,周期采集RTT/丢包
    retryCount = 0;
    source->pollStats();
  }
  // 断线自动重连: 3秒间隔,failed触发,成功后计数清零
  if (bAutoReconnect && bOpenRequested && cs == RtcConnState::failed &&
      retryCount < maxRetryCount) {
    int64_t now = timeStampMS();
    if (retryAtMs == 0) {
      retryAtMs = now + 3000;
    } else if (now >= retryAtMs) {
      retryAtMs = 0;
      retryCount++;
      LOGFLF(LogLevel::info, "rtc auto reconnect, retry:", retryCount);
      cmdReopen();
    }
  }
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
    LOGFLF(LogLevel::warn, "raw source error:", getAVErrorStr(error),
           " msg:", msg);
    MPOB::dispatch(&IMediaPlayerOb::onIoError, error, msg);
  }
}

void RtcPlayer::onVideoFrame(const YUVFrame& frame, int32_t trackId) {
  onRemoteFrame(true);
  // 收到远端 CPU 视频帧,渲染到远程渲染器
  remoteVRender->render(frame);
}

void RtcPlayer::onGpuFrame(const GpuFrame& frame, int32_t trackId) {
  onRemoteFrame(true);
  // 收到远端 GPU 视频帧,渲染到远程渲染器
  remoteVRender->render(frame);
}

void RtcPlayer::onAudioFrame(const AvoxAFrame& frame, int32_t trackId) {
  onRemoteFrame(false);
  // 收到远端音频帧,播放到远程音频渲染器
  if (remoteARender) {
    if (!bAudioDescSet) {
      ATrackDesc trackDesc = source->getAudioDesc(0);
      if (trackDesc.desc.sampleRate > 0) {
        remoteARender->setDesc(trackDesc.desc);
        bAudioDescSet = true;
      }
    }
    if (bAudioDescSet) {
      remoteARender->render(frame.buffer);
    }
  }
}

void RtcPlayer::onClose() { LOGFLF(LogLevel::info, "rtc closed"); }

void RtcPlayer::onRemoteFrame(bool bVideo) {
  // 帧率统计(1秒窗口)
  fpsCount.fetch_add(1, std::memory_order_relaxed);
  int64_t now = timeStampMS();
  int64_t start = fpsTime.load(std::memory_order_relaxed);
  if (start == 0 || now - start >= 1000) {
    if (fpsTime.compare_exchange_strong(start, now)) {
      fpsValue.store(fpsCount.exchange(0, std::memory_order_relaxed),
                     std::memory_order_release);
    }
  }
  if (state == PlayerState::ready) {
    setState(PlayerState::playing);
    // 首帧只对视频派发
    if (bVideo) {
      Observer<IRtcPlayerOb>::dispatch(&IRtcPlayerOb::onFirstVideoFrame);
    }
  }
}

// ISdpAgentOb 实现
void RtcPlayer::onLocalSdp(const char* localSdp) {
  // 信令线程回调, 转发上层钩子与信令通道
  if (userSdpOb) {
    userSdpOb->onLocalSdp(localSdp);
  }
  if (signalChannel) {
    signalChannel->sendLocalSdp(localSdp);
  }
}

void RtcPlayer::onIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) {
  if (userSdpOb) {
    userSdpOb->onIceCandidate(candidate, mid, mlineIndex);
  }
  if (signalChannel) {
    signalChannel->sendIceCandidate(candidate, mid, mlineIndex);
  }
}

// ISignalOb 实现
void RtcPlayer::onRemoteSdp(const char* sdp) {
  // 可能来自通道线程,转播放线程与open/close串行
  if (!sdp) {
    return;
  }
  auto sdpCmd = createCommand<MPCommandType::SetRemoteSdp>(sdp);
  mpCommands.enqueueWait(sdpCmd);
}

void RtcPlayer::onRemoteIceCandidate(const char* candidate, const char* mid,
                                     int mlineIndex) {
  source->addIceCandidate(candidate, mid, mlineIndex);
}

}
