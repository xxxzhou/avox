#pragma once
// Public player interface. Minimal surface: open / transport / speed,
// everything else arrives through the observer.
#include <cstdint>
#include <string_view>

namespace avox {

enum class PlayerState { idle, opening, playing, paused, buffering, closed, error };

class IMediaPlayerOb {
 public:
  virtual ~IMediaPlayerOb() = default;
  virtual void onState(PlayerState state) = 0;
  virtual void onPosition(int64_t ptsMs) = 0;
  virtual void onError(int code, std::string_view message) = 0;
};

class IMediaPlayer {
 public:
  virtual ~IMediaPlayer() = default;
  virtual bool open(std::string_view url) = 0;
  virtual void close() = 0;
  virtual void play() = 0;
  virtual void pause(bool pause) = 0;
  virtual bool seek(int64_t ptsMs) = 0;
  virtual void setSpeed(double speed) = 0;
  virtual void setObserver(IMediaPlayerOb* ob) = 0;
};

}  // namespace avox
