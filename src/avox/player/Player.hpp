#pragma once

#include <string>

#include "../AvoxBase.h"
#include "../AvoxPlayer.h"
#include "../AvoxTime.h"
#include "avox/AvoxCodec.h"
#include "avox/video/VideoBuffer.hpp"
#include "../module/Observer.hpp"

namespace avox {

#define AVOX_MAX_TRACK 4

// 1s
#define AVOX_NOSYNC_THRESHOLD (int64_t)(1000)
#define AVOX_NOVALID_PTS INT64_MIN
// 100ms
#define AVOX_SYNC_THRESHOLD_MAX (int64_t)(100)
#define AVOX_SYNC_THRESHOLD_MIN (int64_t)(40)

// 在MediaPlayer下的资源
class IPlayerContext {
 public:
  virtual ~IPlayerContext();

 protected:
  class MediaPlayer* mediaPlayer = nullptr;
  class MPPingQueue* mpPingback = nullptr;

 public:
  void setMediaPlayer(class MediaPlayer* mp);
  void attachPlayContext(IPlayerContext* context);
  class MediaPlayer* getMediaPlayer() { return mediaPlayer; }
};

class PtsUpdater {
 public:
  PtsUpdater() = default;
  virtual ~PtsUpdater() = default;

 protected:
  // 上一帧的pts
  int64_t prePts = AVOX_NOVALID_PTS;
  // 上次间隔(假定每帧简隔相差不大)
  int64_t duration = 0;

 public:
  int64_t getDuration() { return duration; }

 public:
  void updatePts(int64_t pts) {
    int64_t temp = 0;
    if (prePts != AVOX_NOVALID_PTS) {
      temp = pts - prePts;
    }
    prePts = pts;
    duration = temp;
  }
  void resetPts() {
    prePts = AVOX_NOVALID_PTS;
    duration = 0;
  }
};

class PtsChecker {
 public:
  PtsChecker() = default;
  virtual ~PtsChecker() = default;

 protected:
  // 基准pts,可能会变化
  int64_t basePts = AVOX_NOVALID_PTS;
  // 相对本地时间的基准时间，单位毫秒
  int64_t baseTimeMs = 0;
  // 上一帧的pts
  int64_t prePts = AVOX_NOVALID_PTS;
  // 检查最大的时间差，单位毫秒
  int64_t maxTimeMs = 1000;

 public:
  void recordPts(int64_t pts);
};

}