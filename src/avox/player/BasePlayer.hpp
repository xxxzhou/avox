#pragma once

#include "../AvoxPlayer.h"
#include "../module/JsonOption.hpp"
#include "../module/Observer.hpp"
#include "../module/Ringbuffer.hpp"
#include "../module/RunTask.hpp"
#include "MPCommand.hpp"

namespace avox {

using MPOB = Observer<IMediaPlayerOb>;
using MPOP = Observer<IOptionOb>;

class AVOX_EXPORT BasePlayer : public Observer<IMediaPlayerOb>, public JsonOption {
 public:
  BasePlayer();
  virtual ~BasePlayer();

 protected:
  // 播放状态，任何改变播放状态的动作需要在队列统一线程中执行
  PlayerState state = PlayerState::none;
  // 上个状态
  PlayerState preState = PlayerState::none;
  RingBuffer<MPCommandPtr> mpCommands;

 protected:
  void setState(PlayerState state);

 protected:
  virtual void onSetState() {};
};

}