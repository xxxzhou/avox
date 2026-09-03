#include "HighClock.hpp"

#include <cassert>

using namespace std::chrono;

namespace avox {

HighClock::HighClock(/* args */) { start(); }

HighClock::~HighClock() {}

void HighClock::start() {
  startPoint = high_resolution_clock::now();
  recordPoints.clear();
}

int32_t HighClock::record() {
  recordPoints.push_back(high_resolution_clock::now());
  return recordPoints.size() - 1;
}

int64_t HighClock::recordDelta() {
  int32_t recordIndex = record();
  return getClock(recordIndex);
}

int64_t HighClock::recordLast() {
  int32_t recordIndex = record();
  if (recordIndex == 0) {
    return getClock(recordIndex);
  }
  return getClock(recordIndex - 1, recordIndex);
}

int64_t HighClock::getClock(int32_t recordIndex) {
  assert(recordIndex < recordPoints.size());
  return duration_cast<std::chrono::microseconds>(recordPoints[recordIndex] -
                                                  startPoint)
      .count();
}

int64_t HighClock::getClock(int32_t recordStart, int32_t recordEnd) {
  assert(recordStart < recordPoints.size());
  assert(recordEnd < recordPoints.size());
  return duration_cast<std::chrono::microseconds>(recordPoints[recordEnd] -
                                                  recordPoints[recordStart])
      .count();
}

}
