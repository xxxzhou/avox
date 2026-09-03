#include "AudioTap.hpp"

namespace avox {

AudioTap::AudioTap() { taskName = "audio tap task"; }

AudioTap::~AudioTap() { close(); }

bool AudioTap::open(const AudioDesc& srcDesc_, const AudioDesc& outDesc_,
                    int32_t frameMs_) {
  close();
  srcDesc = srcDesc_;
  // outDesc 为空(sampleRate<=0)时跟随源格式,不重采样
  AudioDesc outDesc = outDesc_;
  if (outDesc.sampleRate <= 0) {
    outDesc = srcDesc;
  }
  // AudioReshaper 用 frameMs 字段,先设好再 initConfig
  frameMs = frameMs_ > 0 ? frameMs_ : 40;
  if (!initConfig(srcDesc, outDesc)) {
    LOGFLF(LogLevel::warn, "audio tap initConfig failed");
    return false;
  }
  bDescDispatched = false;
  // 派发源格式就绪
  dispatch(&IAudioTapOb::onAudioDesc, outDesc);
  bDescDispatched = true;
  bRunning = true;
  LOGFLF(LogLevel::info, "src desc:", srcDesc, " out desc:", outDesc,
         " frame ms:", frameMs);
  startTask();
  return true;
}

void AudioTap::close() {
  if (!bRunning) {
    return;
  }
  bRunning = false;
  queue.setClose(true);
  stopTask();
  queue.clear();
  queue.setClose(false);
  clear();
}

bool AudioTap::bOpen() { return bRunning; }

void AudioTap::push(const AvoxData& raw, int64_t pts) {
  if (!bRunning) {
    return;
  }
  // 深拷贝入队
  AudioFramePtr frame;
  AvoxAFrame aframe = {};
  aframe.buffer = raw;
  aframe.pts = pts;
  copyAudioBuf(frame, aframe);
  // bBlock:true 满则阻塞反压到解码线程,false 满则丢最旧
  if (bBlock) {
    queue.enqueueWait(frame);
  } else {
    queue.enqueue(frame, true);
  }
}

void AudioTap::onProcess() {
  // curFrame 已满(已重采样/切片到 outDesc),dispatch 给 observer
  // 帧仅在回调内有效,curFrame 随即被 clear
  AvoxData data = {curFrame.point(), curFrame.getSize(), true};
  dispatch(&IAudioTapOb::onFrame, data, curFrame.getPts());
}

void AudioTap::onRunTask() {
  while (running()) {
    AudioFrame frame = {};
    bool bGet = queue.dequeueAction(
        [&frame](const AudioFramePtr& cframe) { frame.form(*cframe); });
    if (!bGet) {
      // 空队列,sleep
      sleepTask(true, frameMs / 2);
      continue;
    }
    // 送入 reshaper 累积,满则触发 onProcess → dispatch
    AvoxAFrame aframe = {};
    aframe.buffer = {frame.point(), frame.getSize(), true};
    aframe.pts = frame.getPts();
    process(aframe);
  }
  // stop 后排干队列: close() 不再丢弃 in-flight 尾部帧 —— 全部 reshape → dispatch 喂给上层。
  // close 在 stopTask 前已 queue.setClose(true), 此时 dequeueAction 会因 bClose 返回 false,
  // 故用 drain(忽略 bClose) 逐项取出处理。
  queue.drain([&](const AudioFramePtr& cframe) {
    AvoxAFrame aframe = {};
    aframe.buffer = {cframe->point(), cframe->getSize(), true};
    aframe.pts = cframe->getPts();
    process(aframe);
  });
  // curFrame 里没满的残余也吐出 (AudioReshaper::flush)
  flush();
}

}
