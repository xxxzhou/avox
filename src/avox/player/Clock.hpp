#pragma once

#include "../AvoxDef.h"
#include "../AvoxTime.h"
#include "../module/LogHelper.hpp"
#include "Player.hpp"
#include <utility>


namespace avox {

// 时钟同步方式
enum class SyncType { none, audio, video, external };

// 参考ffplay用来同步时钟
// 主时钟，根据已渲染数据与包校准时间
// 从时钟，根据包与主时钟校准时间，供另外非主时钟比较
class Clock {
public:
  Clock() = default;
  ~Clock() = default;

private:
  // 自身时间(毫秒),每次(变速,暂停)重设基准
  int64_t pts = AVOX_NOVALID_PTS;
  // 偏移 自身PTS与外部时间差(正常播放相对恒定)
  int64_t offset = 0;
  // 上次更新时间,外部时间
  int64_t lastUpdate = 0;
  // 时钟速度
  double speed = 1.0;
  // 是否暂停
  bool bPause = false;

public:
  // 当前PTS时间
  int64_t clock() const {
    if (bPause) {
      return pts;
    }
    if (pts == AVOX_NOVALID_PTS) {
      return AVOX_NOVALID_PTS;
    }
    int64_t now = timeStampMS();
    // 正常情况，offset+now近似当前包的pts,offset+lastUpdate为上次pts
    // now-lastUpdate为实际时间间隔
    // speed为1时，now-lastUpdate不能造成影响
    // speed为0时，now-lastUpdate与二次PTS简隔差不多，结果近拟上次pts
    // speed为0.5时，now-lastUpdate只有实际时间一半，结果pts减半
    // speed为2时，1.0-speed为负1，近似加了二次PTS简隔
    // speed为4时，1.0-speed为负3，近似加了三次PTS简隔
    return offset + now - (now - lastUpdate) * (1.0 - speed);
  }
  // 更新时钟,只要不暂停,offset变化不会太大
  void update(int64_t pts_, int64_t time) {
    pts = pts_;
    lastUpdate = time;
    offset = pts - time;
  }
  void update(int64_t pts_) {
    int64_t now = timeStampMS();
    update(pts_, now);
  }
  void pause(bool bPause_) {
    // 从暂停恢复时,重锚基准,避免把暂停时长计入播放进度导致进度跳变
    if (bPause && !bPause_ && pts != AVOX_NOVALID_PTS) {
      lastUpdate = timeStampMS();
      offset = pts - lastUpdate;
    }
    bPause = bPause_;
  }
  // 使用外部时钟校准当前时钟(当前时钟异常时)
  // 常用音频与视频校准播放器本身时钟
  void sync(Clock *other) {
    int64_t selfCk = clock();
    int64_t otherCk = other->clock();
    // 一般情况下，不会修改当前时钟
    if (otherCk != AVOX_NOVALID_PTS &&
        (selfCk == AVOX_NOVALID_PTS ||
         std::fabs(selfCk - otherCk) > AVOX_NOSYNC_THRESHOLD)) {
      update(otherCk);
    }
  }
  // 速度不能为0,不能无限慢
  void setSpeed(double speed_) { speed = std::max<double>(0.1, speed_); }
  double getSpeed() { return std::max<double>(0.1, speed); }
  void reset() {
    pts = AVOX_NOVALID_PTS;
    offset = 0;
    lastUpdate = 0;
    // speed = 1.0;
  }
};

}
