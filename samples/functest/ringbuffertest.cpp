#include <iostream>
#include <memory>

#include "avox/module/Ringbuffer.hpp"
#include "avox/player/AudioTrack.hpp"

using namespace avox;

void copyBuf1(PacketBufPtr& pack, const AvoxPacket& data) {
  if (pack) {
    pack->form(data);
  }
  pack = std::make_shared<PacketBuf>(data);
}

void test1() {
  RingBuffer<int32_t> ringBuffer(30);
  for (int32_t i = 0; i < 28; i++) {
    ringBuffer.enqueue(i);
  }
  log(LogLevel::info, "ringBuffer add 28:", ringBuffer.size());
  for (size_t i = 0; i < ringBuffer.size(); i++) {
    log(LogLevel::info, "ringBuffer1 index:", i, " v:", ringBuffer[i]);
  }
  for (int32_t i = 0; i < 10; i++) {
    int32_t t = 0;
    ringBuffer.dequeue(t);
  }
  log(LogLevel::info, "ringBuffer push 10:", ringBuffer.size());
  for (size_t i = 0; i < ringBuffer.size(); i++) {
    log(LogLevel::info, "ringBuffer2 index:", i, " v:", ringBuffer[i]);
  }
  for (int32_t i = 0; i < 10; i++) {
    ringBuffer.enqueue(i);
  }
  log(LogLevel::info, "ringBuffer add 10:", ringBuffer.size());
  for (size_t i = 0; i < ringBuffer.size(); i++) {
    log(LogLevel::info, "ringBuffer3 index:", i, " v:", ringBuffer[i]);
  }
  for (int32_t i = 50; i < 60; i++) {
    ringBuffer.enqueue(i);
  }
  log(LogLevel::info, "ringBuffer add 10:", ringBuffer.size());
  for (size_t i = 0; i < ringBuffer.size(); i++) {
    log(LogLevel::info, "ringBuffer4 index:", i, " v:", ringBuffer[i]);
  }
}

void test2() {
  std::vector<uint8_t> bu(100);
  AvoxPacket avp = {};
  avp.data.data = bu.data();
  avp.data.size = bu.size();

  std::vector<uint8_t> bu1(200);
  AvoxPacket avp1 = {};
  avp1.data.data = bu1.data();
  avp1.data.size = bu1.size();

  PacketBufPtr p = std::make_shared<PacketBuf>(avp);
  PacketBufPtr p1 = std::make_shared<PacketBuf>(avp1);

  RingBuffer<PacketBufPtr> ringBuffer(4);
  ringBuffer.enqueue<AvoxPacket>(avp, copyBuf1);
  ringBuffer.enqueue<AvoxPacket>(avp1, copyBuf1);
  ringBuffer.enqueue<AvoxPacket>(avp1, copyBuf1);
  ringBuffer.enqueue<AvoxPacket>(avp1, copyBuf1);
  ringBuffer.enqueue<AvoxPacket>(avp1, copyBuf1);
}

// 进入的快，出入的慢，选择丢弃，出的数据不连续，新数据容易丢
void test3() {
  RingBuffer<int32_t> ringBuffer;
  ringBuffer.setMaxSize(10);
  std::thread t1([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      ringBuffer.enqueue(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  int32_t preV = -1;
  std::thread t2([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      int32_t v = 0;
      if (ringBuffer.dequeue(v)) {
        log(LogLevel::info, "ringBuffer dequeue:", v);
        if (v != preV + 1) {
          log(LogLevel::warn, "----ringBuffer dequeue error:", v);
        }
        preV = v;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  });
  t1.join();
  t2.join();
}

// 进入的快，出入的慢，进入的数据需要等待，不会丢失,长时间队列满
void test4() {
  RingBuffer<int32_t> ringBuffer;
  ringBuffer.setMaxSize(10);
  std::thread t1([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      ringBuffer.enqueueWait(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  int32_t preV = -1;
  std::thread t2([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      int32_t v = 0;
      if (ringBuffer.dequeue(v)) {
        log(LogLevel::info, "ringBuffer dequeue:", v,
            " size:", ringBuffer.size());
        if (v != preV + 1) {
          log(LogLevel::warn, "----ringBuffer dequeue error:", v);
        }
        preV = v;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  t1.join();
  t2.join();
}

// 进入的慢，出入的快，进一个出一个，长时间队列只有一个
void test5() {
  RingBuffer<int32_t> ringBuffer;
  ringBuffer.setMaxSize(10);
  std::thread t1([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      ringBuffer.enqueueWait(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  int32_t preV = -1;
  std::thread t2([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      int32_t v = 0;
      if (ringBuffer.dequeue(v)) {
        log(LogLevel::info, "ringBuffer dequeue:", v,
            " size:", ringBuffer.size());
        if (v != preV + 1) {
          log(LogLevel::warn, "----ringBuffer dequeue error:", v);
        }
        preV = v;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  t1.join();
  t2.join();
}

// 进入的快，出入的慢，选择覆盖,出的数据不连续，老数据容易丢
void test6() {
  RingBuffer<int32_t> ringBuffer;
  ringBuffer.setMaxSize(10);
  std::thread t1([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      ringBuffer.enqueue(i, true);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  int32_t preV = -1;
  std::thread t2([&]() {
    for (int32_t i = 0; i < 1000000; i++) {
      int32_t v = 0;
      if (ringBuffer.dequeue(v)) {
        log(LogLevel::info, "ringBuffer dequeue:", v);
        if (v != preV + 1) {
          log(LogLevel::warn, "----ringBuffer dequeue error:", v);
        }
        preV = v;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  });
  t1.join();
  t2.join();
}

int main() {
  // test1();
  test6();
  return 0;
}
