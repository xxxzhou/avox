#include "BasePlayer.hpp"

namespace avox {

BasePlayer::BasePlayer() {}

BasePlayer::~BasePlayer() {}

void BasePlayer::setState(PlayerState state_) {
  if (state == state_) {
    return;
  }
  preState = state;
  state = state_;
  MPOB::dispatch(&IMediaPlayerOb::onStateChange, preState, state);
  onSetState();
}

}
