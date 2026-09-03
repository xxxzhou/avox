#pragma once

#include <math.h>
#include "AvoxDef.h"
// 深入聊聊时间码 https://zhuanlan.zhihu.com/p/101728723
// 参照UE4相关实现

namespace avox {

/** The maximum number of ticks that can be represented in FTimespan. */
constexpr int64_t maxTicks = 9223372036854775807;
/** The minimum number of ticks that can be represented in FTimespan. */
constexpr int64_t minTicks = -9223372036854775807 - 1;
/** The number of nanoseconds per tick. */
constexpr int64_t nanosecondsPerTick = 100;
/** The number of timespan ticks per day. */
constexpr int64_t ticksPerDay = 864000000000;
/** The number of timespan ticks per hour. */
constexpr int64_t ticksPerHour = 36000000000;
/** The number of timespan ticks per microsecond. */
constexpr int64_t ticksPerMicrosecond = 10;
/** The number of timespan ticks per millisecond. */
constexpr int64_t ticksPerMillisecond = 10000;
/** The number of timespan ticks per minute. */
constexpr int64_t ticksPerMinute = 600000000;
/** The number of timespan ticks per second. */
constexpr int64_t ticksPerSecond = 10000000;
/** The number of timespan ticks per week. */
constexpr int64_t ticksPerWeek = 6048000000000;
/** The number of timespan ticks per year (365 days, not accounting for leap
 * years). */
constexpr int64_t ticksPerYear = 365 * ticksPerDay;
constexpr double smallFps = 1.e-4;

enum class RateMode : int32_t {
  none = 0,
  rate24,
  rate25,
  // 30000, 1001
  rate30Drop,
  rate30,
  rate48,
  rate50,
  // 60000, 1001
  rate60Drop,
  rate60,
  rate100,
  rate120,
  rate144,
  // 所有Rate个数
  rateCount,
};

inline double roundToZero(double value) {
  return (value < 0.0) ? ceill(value) : floorl(value);
}

// 同步所用表示桢数
struct FrameSize {
  int32_t frameNumber = 0;
  float subFrame = 0.0;
};

// 桢率,更精确的表示方法
struct FrameRate {
  int32_t numerator = 50;
  int32_t denominator = 1;
  // 帧率显示
  inline double asDecimal() const {
    if (denominator == 0) {
      return 0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
  }
  // 每帧间隔(秒)
  inline double asInterval() const {
    if (numerator == 0) {
      return 0;
    }
    return static_cast<double>(denominator) / static_cast<double>(numerator);
  }
  // 帧数表示的秒
  inline double asSeconds(const FrameSize& frameSize) const {
    int64_t part = (int64_t)frameSize.frameNumber * denominator;
    double subPart = static_cast<double>(frameSize.subFrame * denominator);
    return (static_cast<double>(part) + subPart) / numerator;
  }
  // 从秒返回桢数
  inline FrameSize asFrameSize(double timeSeconds) const {
    double timeAsFrame = (timeSeconds * numerator) / denominator;
    int32_t frameNumber = floorl(timeAsFrame);
    float subFrame = timeAsFrame - floorl(timeAsFrame);
    FrameSize result = {frameNumber, subFrame};
    return result;
  }
  // 29.97 or 59.94
  inline bool useDrop() const {
    double rate = asDecimal();
    if (fabsl(rate - 30.0 / 1.001) <= smallFps ||
        fabsl(rate - 60.0 / 1.001) <= smallFps) {
      return true;
    }
    return false;
  }
  inline void build(double fps) {
    // (24,48 film) (30,60 NTSC)
    double vfps = fps * 1.001;
    double vnum = roundl(vfps);
    if (fabsl(vnum - vfps) <= smallFps) {
      numerator = vnum * 1000;
      denominator = 1001;
    } else {
      numerator = (int32_t)fps;
      denominator = 1;
    }
  }
  inline void build(const RateMode& rateMode) {
    static FrameRate frameRates[(int32_t)RateMode::rateCount] = {
        {0, 0},  {24, 1},       {25, 1}, {30000, 1001}, {30, 1},  {48, 1},
        {50, 1}, {60000, 1001}, {60, 1}, {100, 1},      {120, 1}, {140, 1}};
    if (rateMode == RateMode::rateCount) {
      numerator = 0;
      denominator = 0;
    }
    *this = frameRates[static_cast<int>(rateMode)];
  }
};

struct FrameTime {
  FrameSize frameSize = {};
  FrameRate frameRate = {};

  inline double asSeconds() const { return frameRate.asSeconds(frameSize); }
};

// 时间戳表示
struct Timespan {
  // 100 纳秒
  int64_t ticks = 0;

  // 每个tick为100纳秒
  inline void build(int64_t mticks) { ticks = mticks; }

  inline void build(int32_t days, int32_t hours, int32_t minutes,
                    int32_t seconds, int32_t milliSeconds) {
    int64_t total = 0;
    total += days * ticksPerDay;
    total += hours * ticksPerHour;
    total += minutes * ticksPerMinute;
    total += seconds * ticksPerSecond;
    total += milliSeconds * ticksPerMillisecond;
    ticks = total;
  }

  inline void build(int32_t hours, int32_t minutes, int32_t seconds) {
    build(0, hours, minutes, seconds, 0);
  }
  // 时间
  inline int32_t getHours() const {
    return (int32_t)((ticks / ticksPerHour) % 24);
  }
  // 分
  inline int32_t getMinutes() const {
    return (int32_t)((ticks / ticksPerMinute) % 60);
  }
  // 秒
  inline int32_t getSeconds() const {
    return (int32_t)((ticks / ticksPerSecond) % 60);
  }
  // 毫秒
  inline int32_t getMilliSeconds() const {
    return (int32_t)((ticks % ticksPerSecond) / ticksPerMillisecond);
  }

  // 时间
  inline double getTotalHours() const {
    return static_cast<double>(ticks) / ticksPerHour;
  }
  // 分
  inline double getTotalMinutes() const {
    return static_cast<double>(ticks) / ticksPerMinute;
  }
  // 秒
  inline double getTotalSeconds() const {
    return static_cast<double>(ticks) / ticksPerSecond;
  }
  // 毫秒
  inline double getTotalMilliSeconds() const {
    return static_cast<double>(ticks) / ticksPerMillisecond;
  }
  inline bool zero() const { return ticks == 0; }
};

// 丢帧时间码,30fps一小时108000帧,29.97一小时是107892帧,只有107892帧,需要显示108000帧
// 解决方法,每小时相差108帧,每十分钟丢18帧,规定每十分钟的0-9每分钟丢2帧,而10分钟时不丢帧.
// 这样,在第N个十分钟中的第K分钟,实际帧数显示成时码帧数为:(N-1)*18+K*2
struct Timecode {
  int32_t hours = 0;
  int32_t minutes = 0;
  int32_t seconds = 0;
  // 当前秒下桢数
  int32_t frames = 0;
  // 丢桢时间码,如NTSC带小数点格式
  bool bDropFrame = false;
  inline void build(const Timespan& timespan, const FrameRate& frameRate,
                    bool dropFrame) {
    double totalSeconds = timespan.getTotalSeconds();
    build(totalSeconds, frameRate, dropFrame);
  }
  inline void build(double seconds_, const FrameRate& frameRate,
                    bool dropFrame) {
    int32_t numFrames =
        dropFrame
            ? (int32_t)roundl(seconds_ * frameRate.asDecimal())
            : (int32_t)roundl(seconds_ * roundl(frameRate.asDecimal()));
    build(numFrames, frameRate, dropFrame);
  }
  inline void build(int32_t frameNumber, const FrameRate& frameRate) {
    build(frameNumber, frameRate, frameRate.useDrop());
  }
  inline void build(int32_t frameNumber, const FrameRate& frameRate,
                    bool dropFrame) {
    // 向上取整,29.97=30帧
    const int32_t frameInSecond = (int32_t)ceill(frameRate.asDecimal());
    const int32_t frameInMinute = frameInSecond * 60;
    const int32_t frameInHour = frameInMinute * 60;
    if (frameInSecond <= 0) {
      build(0, 0, 0, 0, dropFrame);
      return;
    }
    // 创建丢帧时间码
    if (dropFrame) {
      // 29.97桢丢帧2,59.94丢帧4
      const int32_t numberDrop = frameInSecond <= 30 ? 2 : 4;
      // 当前帧率每十分钟多少桢(30FPS-18000,29.97FPS-17982)
      const int32_t numberTenMinutes =
          (int32_t)floorl(frameRate.asDecimal() * 60 * 10);
      // 几个十分钟
      const int32_t skipFrames = fabsl(frameNumber) / numberTenMinutes;
      // 一共跳多少帧,每个十分钟后九分钟都跳numberDrop
      const int32_t skipTotal = skipFrames * 9 * numberDrop;
      int32_t offsetFrame = fabsl(frameNumber);
      // 在十分钟桢的那帧
      int32_t frameInTrueFrames = offsetFrame % numberTenMinutes;
      // 29.97(0,1) 59.94(0,1,2,3) 不跳时间码
      if (frameInTrueFrames < numberDrop) {
        offsetFrame += skipTotal;
      } else {
        // 每分钟需要同步帧
        const uint32_t numTrueMinute =
            (uint32_t)floorl(frameRate.asDecimal() * 60);
        // 在十分钟的那一分钟
        int32_t minuteOfTen = (frameInTrueFrames - numberDrop) / numTrueMinute;
        // 一共跳多少桢
        int32_t numAddFrames = skipTotal + (numberDrop * minuteOfTen);
        offsetFrame += numAddFrames;
      }
      frameNumber = offsetFrame * (frameNumber > 0 ? 1 : -1);
    }
    int32_t hours_ = (int32_t)roundToZero(frameNumber / frameInHour);
    int32_t minutes_ = (int32_t)roundToZero(frameNumber / frameInMinute) % 60;
    int32_t seconds_ = (int32_t)roundToZero(frameNumber / frameInSecond) % 60;
    int32_t frames_ = frameNumber % frameInSecond;
    build(hours_, minutes_, seconds_, frames_, dropFrame);
  }
  inline void build(int32_t hours_, int32_t minutes_, int32_t seconds_,
                    int32_t frames_, bool dropFrame) {
    hours = hours_;
    minutes = minutes_;
    seconds = seconds_;
    frames = frames_;
    bDropFrame = dropFrame;
  }
  inline int32_t toFrameNumber(const FrameRate& frameRate) const {
    // 向上取整,29.97=30帧
    const int32_t frameInSecond = (int32_t)ceill(frameRate.asDecimal());
    const int32_t frameInMinute = frameInSecond * 60;
    const int32_t frameInHour = frameInMinute * 60;
    if (frameInSecond <= 0) {
      return 0;
    }
    // 确保frames与帧率 (frames大于frameInSecond)
    int32_t safeSeconds = seconds + frames / frameInSecond;
    int32_t safeFrames = frames % frameInSecond;
    int32_t safeMinutes = minutes + safeSeconds / 60;
    safeSeconds = safeSeconds % 60;
    int32_t safeHours = hours + safeMinutes / 60;
    safeMinutes = safeMinutes % 60;
    // 丢帧情况下
    if (bDropFrame) {
      const int32_t numberDrop = frameInSecond <= 30 ? 2 : 4;
      // 一共多少分
      int32_t totalMinutes = (safeHours * 60) + safeMinutes;
      // 跳过10,每九分钟丢弃18帧
      int32_t totalDropped =
          numberDrop *
          (totalMinutes - (int32_t)roundToZero(totalMinutes / 10.0));
      int32_t totalFrames =
          (safeHours * frameInHour) + (safeMinutes * frameInMinute) +
          (safeSeconds * frameInSecond) + safeFrames - totalDropped;
      // 返回实际帧数
      return totalFrames;
    } else {
      int32_t totalFrames = (safeHours * frameInHour) +
                            (safeMinutes * frameInMinute) +
                            (safeSeconds * frameInSecond) + safeFrames;
      return totalFrames;
    }
  }
  inline Timespan toTimespan(const FrameRate& frameRate) const {
    // 一共多少帧
    const int32_t frameNumber = toFrameNumber(frameRate);
    // 一共多少秒
    const double numberOfSeconds = bDropFrame
                                       ? frameNumber * frameRate.asInterval()
                                       : static_cast<double>(frameNumber) /
                                             roundl(frameRate.asDecimal());
    // 对应多少tick
    int64_t tick = (int64_t)floorl(numberOfSeconds * ticksPerSecond + 0.5);
    Timespan timespan = {tick};
    return timespan;
  }
  inline void copyFrom(const Timecode& src) {
    hours = src.hours;
    minutes = src.minutes;
    seconds = src.seconds;
    frames = src.frames;
    bDropFrame = src.bDropFrame;
  }
};

extern "C" {
// 获取时间(毫秒)=10000tick
AVOX_EXPORT int64_t timeStampMS();
// 获取本地时间戳(毫秒)
AVOX_EXPORT int64_t localTimeStampMS();
// 本地时间Tick
AVOX_EXPORT int64_t localTimeMS(int64_t utcMS);
// 本地时间Tick
AVOX_EXPORT int64_t localTimeTick(int64_t utcTick);
// 得到高精度当前Tick,以100ns计时,1ms=10000tick
AVOX_EXPORT int64_t timeTick();
// 以double表示的高精度毫秒时间
AVOX_EXPORT double timeTickMS();
// 1Tick = 100ns
AVOX_EXPORT int64_t getFrameTick(double fps);
// sleep
AVOX_EXPORT bool sleepToTick(int64_t timeTarget);
// 根据帧率与帧数得到对应时间Tick(100ns)
AVOX_EXPORT int64_t getFrameRateTick(const FrameRate& frameRate,
                                    int32_t frameNumber);
}

}