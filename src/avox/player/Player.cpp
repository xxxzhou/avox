#include "Player.hpp"

#include "../source/DeviceSource.hpp"
#include "BasePlayer.hpp"
#include "MPPingback.hpp"
#include "MediaPlayer.hpp"
#include "SourcePlayer.hpp"

namespace avox {

IMediaPlayer* createMediaPlayer() { return new MediaPlayer(); }

void addMediaPlayerOb(IMediaPlayer* player, IMediaPlayerOb* ob) {
  MediaPlayer* mp = dynamic_cast<MediaPlayer*>(player);
  if (mp) {
    mp->Observer<IMediaPlayerOb>::addObserver(ob);
  }
}

void removeMediaPlayerOb(IMediaPlayer* player, IMediaPlayerOb* ob) {
  MediaPlayer* mp = dynamic_cast<MediaPlayer*>(player);
  if (mp) {
    mp->Observer<IMediaPlayerOb>::removeObserver(ob);
  }
}

ISourcePlayer* createDevicePlayer() { return new SourcePlayer(); }

void addSourcePlayerOb(ISourcePlayer* player, IMediaPlayerOb* ob) {
  SourcePlayer* sp = dynamic_cast<SourcePlayer*>(player);
  if (sp) {
    sp->Observer<IMediaPlayerOb>::addObserver(ob);
  }
}

void removeSourcePlayerOb(ISourcePlayer* player, IMediaPlayerOb* ob) {
  SourcePlayer* sp = dynamic_cast<SourcePlayer*>(player);
  if (sp) {
    sp->Observer<IMediaPlayerOb>::removeObserver(ob);
  }
}

// RtcPlayer : public IRtcPlayer, public BasePlayer (多继承)
// dynamic_cast<BasePlayer*> 做 cross-cast, 核心层无需知道 RtcPlayer 具体类
void addRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob) {
  BasePlayer* bp = dynamic_cast<BasePlayer*>(player);
  if (bp) {
    bp->Observer<IMediaPlayerOb>::addObserver(ob);
  }
}

void removeRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob) {
  BasePlayer* bp = dynamic_cast<BasePlayer*>(player);
  if (bp) {
    bp->Observer<IMediaPlayerOb>::removeObserver(ob);
  }
}

const char* getIoPlanStr(IoPlan plan) {
  switch (plan) {
#define XX(name, value, str) \
  case IoPlan::name:         \
    return str;
    AVOX_MAP_IO_PLAN_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getPlayerStateStr(PlayerState state) {
  switch (state) {
#define XX(name, value, str) \
  case PlayerState::name:    \
    return str;
    AVOX_MAP_PLAYER_STATE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getTrackTypeStr(TrackType type) {
  switch (type) {
#define XX(name, value, str) \
  case TrackType::name:      \
    return str;
    AVOX_MAP_TRACK_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getDecodeResultStr(DecodeResult error) {
  switch (error) {
#define XX(name, value, str) \
  case DecodeResult::name:   \
    return str;
    AVOX_MAP_DECODE_RESULT(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getARenderTypeStr(ARenderType type) {
  switch (type) {
#define XX(name, value, str) \
  case ARenderType::name:    \
    return str;
    AVOX_MAP_AUDIO_RENDER_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getConfigAddTypeStr(ConfigAddType type) {
  switch (type) {
#define XX(name, value, str) \
  case ConfigAddType::name:  \
    return str;
    AVOX_MAP_CONFIG_ADD(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getSpeedTypeStr(SpeedType type) {
  switch (type) {
#define XX(name, value, str) \
  case SpeedType::name:      \
    return str;
    AVOX_MAP_SPEED_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

IPlayerContext::~IPlayerContext() {
}

void IPlayerContext::setMediaPlayer(MediaPlayer* mp) {
  mediaPlayer = mp;
  mpPingback = mediaPlayer->getMPPingback();
}

void IPlayerContext::attachPlayContext(IPlayerContext* context) {
  if (context) {
    setMediaPlayer(context->getMediaPlayer());
  }
}

void PtsChecker::recordPts(int64_t pts) {
  if (basePts == AVOX_NOVALID_PTS) {
    baseTimeMs = timeStampMS();
    basePts = pts;
    prePts = pts;
  }
  // 前后帧相差比较大，说明时间戳有问题
  if (std::abs(pts - prePts) > maxTimeMs) {
  }
}

}
