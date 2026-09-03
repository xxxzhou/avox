#include <iostream>
#include <thread>

#include "avox/AvoxTime.h"
#include "avox/module/LogHelper.hpp"
using namespace avox;

int main() {
  int32_t i = 0;
  while (i < 12) {
    log(LogLevel::info, "hello world");
    log(LogLevel::info, "gettime:", timeStampMS());
    log(LogLevel::info, "timeTick:", timeTick());
    Timespan startTime = {timeStampMS() * 10000};
    log(LogLevel::info, "1-", startTime);
    Timespan startTime2 = {localTimeStampMS() * 10000};
    log(LogLevel::info, "2-", startTime2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    i++;
  }

  return 0;
}
