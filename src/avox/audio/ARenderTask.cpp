#include "ARenderTask.hpp"

#include "../module/AvoxManager.hpp"
#include "../player/AudioTrack.hpp"
#include "../player/MediaPlayer.hpp"

namespace avox {

ARenderTask::ARenderTask() {
  taskName = "audio render task";
#ifdef AVOX_ENABLE_FFMPEG
  speedResample = std::make_unique<FFResample>();
  syncResmaple = std::make_unique<FFResample>();
#endif
  audioRender = std::unique_ptr<AudioOutput>(getDefaultAudioOutput());
}

ARenderTask::~ARenderTask() {}

void ARenderTask::start(class AudioTrack* context) {
  close();
  trackContext = context;
  // 播放器设置传递给当前对象
  attachPlayContext(trackContext);
  renderDesc = trackContext->getDecodeDesc();
  frameMs = trackContext->getFrameMS();
  if (audioRender) {
    audioRender->setDesc(renderDesc, frameMs);
  }
  // 开始渲染
  startTask();
}

IAudioRender* ARenderTask::getAudioRender() { return audioRender.get(); }

void ARenderTask::pause(bool pause) {
  if (pause) {
    pauseTask();
  } else {
    resumeTask();
  }
  if (audioRender) {
    audioRender->pause(pause);
  }
}

void ARenderTask::speed(double speed) {
  if (audioRender) {
    audioRender->speed(speed);
  }
}

void ARenderTask::flush() {
  if (audioRender) {
    audioRender->flush();
  }
  framePts = 0;
}

void ARenderTask::close() {
  stopTask();
  if (audioRender) {
    audioRender->close();
  }
}

void ARenderTask::onRunTask() {
  AudioFrame frame = {};
  auto frameAction = [&](const AudioFramePtr& cframe) { frame.form(*cframe); };
  // PTS时间基准，用于精确控制渲染节奏
  int64_t baseTime = 0;
  int64_t basePts = 0;
  bool baseSet = false;
  // cframe帧长时间,前面一般会重组成40ms,可以自己定义
  int32_t frameMs = trackContext->getFrameMS();
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    // 倍速变化大,就不渲染声音了
    double speed = trackContext->getClock()->getSpeed();
    bool bIFrameMode = false;
    if (mediaPlayer) {
      bIFrameMode = mediaPlayer->getIFrameMode();
    }
    if (speed > 4 || speed < 0.2 || bIFrameMode) {
      bool bGet = trackContext->getFrameQueue().dequeueAction(frameAction);
      if (bGet) {
        trackContext->updateClock(frame.getPts());
      }
      sleepTask(!bGet, frameMs);
      baseSet = false;  // 重置基准
      continue;
    }
    // 加上速度影响
    int32_t cframeMs = frameMs / speed;
    // 渲染器队列数据很满了，就不要再获取数据了
    bool bWait = audioRender->full();
    if (!bWait) {
      // 1 获取队列里音频数据
      bool bGet = trackContext->getFrameQueue().dequeueAction(frameAction);
      if (!bGet) {
        // 渲染器队列与帧队列里都没数据了,通知播放器buffing
        if (audioRender->empty()) {
          trackContext->onFrameResult(false);
          sleepTask(false, cframeMs / 2);
        }
        baseSet = false;  // 重置基准
        continue;
      }
      int64_t currentPts = frame.getPts();
      int64_t now = timeStampMS();
      // 首帧,或相邻帧PTS跳变(seek)时重置基准
      // 必须用 currentPts - framePts(上一帧PTS)检测跳变:basePts 是固定锚点,
      // 正常播放时 currentPts-basePts 会持续增长,必然超过 frameMs*2 误触发
      // 暂停恢复由后面 sleepMs 偏差检测处理(墙钟跳了,但相邻帧PTS连续)
      if (!baseSet || std::abs(currentPts - framePts) > frameMs * 2) {
        baseTime = now;
        basePts = currentPts;
        baseSet = true;
      }
      AvoxData inData = {frame.point(), frame.getSize(), true};
      // 在这可以应用速度变化如SoundTouch处理变化后的数据
      if (speed != 1.0) {
        AudioDesc changeDesc = renderDesc;
        // 倍速变化,改变采样率
        changeDesc.sampleRate = renderDesc.sampleRate / speed;
#ifdef AVOX_ENABLE_FFMPEG
        speedResample->init(renderDesc, changeDesc);
        int ret = speedResample->resample(inData);
        if (ret <= 0) {
          LOGFLF(LogLevel::warn, "audio speed resample failed, ret:", ret,
                 " speed:", speed, " inSize:", frame.getSize());
        }
#endif
      } else {
        // 暂时只在正常速度下同步外部时钟,确定是否需要改变数据量
        int32_t wanted_bytes = trackContext->syncAudio();
        // 音频不是主时钟,同步外部时钟检测需要调整数据量
        if (wanted_bytes != trackContext->getFrameSize()) {
          AudioDesc changeDesc = renderDesc;
          changeDesc.sampleRate =
              renderDesc.sampleRate * wanted_bytes / inData.size;
#ifdef AVOX_ENABLE_FFMPEG
          syncResmaple->init(renderDesc, changeDesc);
          int ret = syncResmaple->resample(inData);
          if (ret <= 0) {
            LOGFLF(LogLevel::warn, "audio speed resample failed, ret:", ret,
                   " insample:", renderDesc.sampleRate,
                   " outsample:", changeDesc.sampleRate);
          }
#endif
        }
      }
      // 给渲染器inData
      audioRender->render(inData, currentPts);
      // 相机录制
      trackContext->muxerFrame(frame);
      // 记录当前包时间
      framePts = currentPts;
      // 检查渲染器队列还多少数据没渲染
      int64_t queuedMS = audioRender->getQueueMS();
      int64_t playPts = framePts - queuedMS;
      // 更新时钟
      trackContext->updateClock(playPts);
      trackContext->onFrameResult(true);
      // 基于PTS计算下次渲染的预期时间
      int64_t expectedTime = baseTime + (currentPts - basePts) / speed;
      int64_t sleepMs = expectedTime - now;
      if (sleepMs > 0 && sleepMs < cframeMs * 2) {
        if (sleepMs > 5) {
          std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
      } else if (sleepMs > cframeMs * 2 || sleepMs < -cframeMs * 2) {
        // 偏差太大，重置基准
        baseTime = now;
        basePts = currentPts;
      }
    } else {
      // 缓冲区满，等渲染消费
      std::this_thread::sleep_for(std::chrono::milliseconds(cframeMs / 2));
    }
  }
}

}
