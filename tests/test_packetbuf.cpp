// PacketBuf 容量/分配策略单测
// 1) 贴合复用: 不带池时容量不超实际尺寸 4 倍才复用, 大 I 帧不把小包撑大
// 2) 池: 按尺寸档取块, 小包永远小档, I/P 交替时两块在池里轮转(稳态零分配)
// 3) 队列槽回绕: 下游还扣着包时必须换新对象(48bc3bd 起播首 GOP 花屏的回归)
#include <doctest.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "avox/module/Ringbuffer.hpp"
#include "avox/source/PacketBuf.hpp"
#include "avox/source/PacketBufPool.hpp"

namespace avox {

namespace {

// annexb4 开头: checkAvccPacket 判 false 且不做 3->4 转换, form 后 size 即原长
// annexb3: 00 00 01 65 开头, form 会补成 4 字节头(size+1, buff[0]=0)
AvoxPacket makePacket(int32_t size, bool annexb3 = false, uint8_t fill = 0x11,
                      int64_t pts = 0) {
  static std::vector<uint8_t> storage;
  storage.assign(size > 4 ? (size_t)size : (size_t)4, fill);
  if (annexb3) {
    storage[0] = 0x00;
    storage[1] = 0x00;
    storage[2] = 0x01;
    storage[3] = 0x65;
  } else {
    storage[0] = 0x00;
    storage[1] = 0x00;
    storage[2] = 0x00;
    storage[3] = 0x01;
  }
  AvoxPacket pkt = {};
  pkt.packtype = (int32_t)PackType::video;
  pkt.frameType = 1;
  pkt.pts = pts;
  pkt.data.data = storage.data();
  pkt.data.size = size;
  return pkt;
}

}

TEST_CASE("PacketBuf: 大 I 帧后的小包不叠留超大容量") {
  PacketBuf buf;
  buf.form(makePacket(1024 * 1024));
  CHECK(buf.size == 1024 * 1024);
  // 1MB 之后来个 8KB 的 P 帧: 容量要按实际尺寸重分配, 不再留着 1MB
  buf.form(makePacket(8 * 1024));
  CHECK(buf.size == 8 * 1024);
  CHECK(buf.buff.capacity() <= (size_t)8 * 1024 * 4);
  CHECK(buf.buff.capacity() < (size_t)1024 * 1024);
}

TEST_CASE("PacketBuf: 容量贴合时原位复用不重新分配") {
  PacketBuf buf;
  buf.form(makePacket(100 * 1024));
  const uint8_t* ptr = buf.buff.data();
  const size_t cap = buf.buff.capacity();
  // 100KB 容量服务 80KB 包, 在 4 倍贴合区间内 -> 不该重分配
  buf.form(makePacket(80 * 1024));
  CHECK(buf.size == 80 * 1024);
  CHECK(buf.buff.data() == ptr);
  CHECK(buf.buff.capacity() == cap);
}

TEST_CASE("PacketBuf: 小包档(音频量级)容量不抖动") {
  PacketBuf buf;
  buf.form(makePacket(400));
  const uint8_t* ptr = buf.buff.data();
  // 小包容量 <= 64KB 门槛, 只要装得下就一直复用, 不因尺寸起伏重分配
  const int32_t sizes[] = {300, 200, 350, 120, 400};
  for (int32_t size : sizes) {
    buf.form(makePacket(size));
    CHECK(buf.size == size);
    CHECK(buf.buff.data() == ptr);
  }
}

TEST_CASE("PacketBuf: annexb3 转 4 语义不变") {
  PacketBuf buf;
  buf.form(makePacket(20, true));
  CHECK(buf.size == 21);
  CHECK(buf.prefixSize == 4);
  CHECK(buf.buff[0] == 0x00);
  CHECK(buf.buff[1] == 0x00);
  CHECK(buf.buff[2] == 0x00);
  CHECK(buf.buff[3] == 0x01);
  CHECK(buf.buff[4] == 0x65);
}

TEST_CASE("PacketBuf 池: 小包永远拿小档, 大 I 帧不把它撑大") {
  auto pool = std::make_shared<PacketBufPool>();
  PacketBuf buf;
  buf.form(makePacket(1024 * 1024), pool);
  CHECK(buf.size == 1024 * 1024);
  buf.form(makePacket(8 * 1024), pool);
  CHECK(buf.size == 8 * 1024);
  // 8KB 落在首档, 不该复用 1MB 的块
  CHECK(buf.buff.capacity() <= (size_t)32 * 1024);
}

TEST_CASE("PacketBuf 池: 同档反复 form 不重新分配") {
  auto pool = std::make_shared<PacketBufPool>();
  PacketBuf buf;
  buf.form(makePacket(1000 * 1000), pool);
  const uint8_t* ptr = buf.buff.data();
  for (int32_t i = 0; i < 4; ++i) {
    buf.form(makePacket(900 * 1000 + i), pool);
    CHECK(buf.buff.data() == ptr);
  }
}

TEST_CASE("PacketBuf 池: I/P 交替时两块在池里轮转") {
  auto pool = std::make_shared<PacketBufPool>();
  PacketBuf buf;
  buf.form(makePacket(1024 * 1024), pool);
  const uint8_t* ptrI = buf.buff.data();
  buf.form(makePacket(8 * 1024), pool);  // 小包: I 的块回池, 换小档
  CHECK(buf.buff.data() != ptrI);
  buf.form(makePacket(1024 * 1024), pool);  // 下一个 I 帧取回同一块, 不再 malloc
  CHECK(buf.buff.data() == ptrI);
}

TEST_CASE("PacketBuf 池: 池块随对象析构归还") {
  auto pool = std::make_shared<PacketBufPool>();
  CHECK(pool->heldBytes() == 0);
  {
    PacketBufPtr pkt = std::make_shared<PacketBuf>();
    pkt->form(makePacket(1024 * 1024), pool);
    CHECK(pool->heldBytes() == 0);
    pkt.reset();
  }
  CHECK(pool->heldBytes() >= 1024 * 1024);
  // 再取同档: 命中池里那块, 取走后池又归零
  PacketBuf again;
  again.form(makePacket(1024 * 1024), pool);
  CHECK(pool->heldBytes() == 0);
}

TEST_CASE("PacketBuf 池: 对象复用/字段清零") {
  auto pool = std::make_shared<PacketBufPool>();
  PacketBufPtr a = PacketBufPtr(pool->objAcquire(),
      [pool](PacketBuf* q) { pool->objRelease(q); });
  PacketBuf* raw = a.get();
  CHECK(pool->statObjNews() == 1);
  a->form(makePacket(64 * 1024), pool);
  CHECK(a->size == 64 * 1024);
  a.reset();  // recycle: 块回块池, 对象回空闲表
  CHECK(pool->statObjHits() == 0);
  CHECK(pool->statObjNews() == 1);
  PacketBufPtr b = PacketBufPtr(pool->objAcquire(),
      [pool](PacketBuf* q) { pool->objRelease(q); });
  CHECK(b.get() == raw);  // 对象复用, 零 new
  CHECK(b->size == 0);    // 字段已清零, 不会漏旧状态
  CHECK(b->buff.empty());
  b->form(makePacket(8 * 1024), pool);
  CHECK(b->size == 8 * 1024);
  CHECK(b->buff[4] == 0x11);
}

TEST_CASE("PacketBuf: 队列槽回绕不改写被下游扣住的包") {
  auto pool = std::make_shared<PacketBufPool>();
  RingBuffer<PacketBufPtr> queue(2);
  auto put = [&](int32_t size, uint8_t fill) {
    queue.enqueueWait<AvoxPacket>(
        makePacket(size, false, fill),
        [&](PacketBufPtr& pack, const AvoxPacket& pkt) {
          copyBuf(pack, pkt, pool);
        });
  };
  // 槽0 放 64KB, 出队后 held 仍持着(下游扣住)
  put(64 * 1024, 0x11);
  PacketBufPtr held;
  REQUIRE(queue.dequeue(held));
  REQUIRE(held->size == 64 * 1024);
  // 槽1 走一遍, 把读写位置推回槽0
  put(8 * 1024, 0x22);
  PacketBufPtr tmp;
  REQUIRE(queue.dequeue(tmp));
  tmp.reset();
  // 回绕写槽0: held 还持着, 必须换新对象而不是就地改写
  put(4 * 1024, 0x33);
  CHECK(held->size == 64 * 1024);
  CHECK(held->buff[4] == 0x11);  // 负载仍是 0x11, 没被新包改写
  PacketBufPtr back;
  REQUIRE(queue.peek(back));
  CHECK(back.get() != held.get());
  CHECK(back->size == 4 * 1024);
}

TEST_CASE("PacketBuf: 槽对象无人持有时原地复用(省一次 new)") {
  auto pool = std::make_shared<PacketBufPool>();
  RingBuffer<PacketBufPtr> queue(2);
  auto put = [&](int32_t size) {
    queue.enqueueWait<AvoxPacket>(
        makePacket(size),
        [&](PacketBufPtr& pack, const AvoxPacket& pkt) {
          copyBuf(pack, pkt, pool);
        });
  };
  put(16 * 1024);
  PacketBufPtr first;
  REQUIRE(queue.dequeue(first));
  PacketBuf* slotObj = first.get();
  first.reset();  // 下游用完放掉, 槽内对象回到独占
  put(16 * 1024);
  PacketBufPtr second;
  REQUIRE(queue.dequeue(second));
  second.reset();
  put(16 * 1024);  // 回绕到槽0: 独占 -> 复用同一个对象
  PacketBufPtr third;
  REQUIRE(queue.peek(third));
  CHECK(third.get() == slotObj);
}

// 线程安全版: 缓冲在调用栈上(form 同步拷贝), 供多线程用例
AvoxPacket makePacketTs(int32_t size, uint8_t fill = 0x11) {
  // thread_local 保活: form 同步拷贝后包指针才失效, 9MB 大块 free 即 SIGSEGV
  static thread_local std::vector<uint8_t> storage;
  storage.assign((size_t)size > 4 ? (size_t)size : (size_t)4, fill);
  storage[0] = 0x00;
  storage[1] = 0x00;
  storage[2] = 0x00;
  storage[3] = 0x01;
  AvoxPacket pkt = {};
  pkt.packtype = (int32_t)PackType::video;
  pkt.frameType = 1;
  pkt.data.data = storage.data();
  pkt.data.size = size;
  return pkt;
}

TEST_CASE("PacketBuf 池: 超 8M 未入池块不挂池引用(泄漏回归)") {
  std::weak_ptr<PacketBufPool> wp;
  {
    auto pool = std::make_shared<PacketBufPool>();
    wp = pool;
    PacketBufPtr a = PacketBufPtr(pool->objAcquire(),
        [pool](PacketBuf* q) { pool->objRelease(q); });
    a->form(makePacketTs(9 * 1024 * 1024), pool);  // 超 8M: poolClass=-1 但 pool 成员置位
    a.reset();  // 修复前: 囤积对象仍持 pool 引用 → 池永不析构(泄漏 32MB+16 对象)
  }
  CHECK(wp.expired());
}

TEST_CASE("PacketBuf 池: 多线程 acquire/release 冒烟") {
  auto pool = std::make_shared<PacketBufPool>();
  auto worker = [pool]() {
    for (int32_t i = 0; i < 300; ++i) {
      PacketBufPtr p = PacketBufPtr(pool->objAcquire(),
          [pool](PacketBuf* q) { pool->objRelease(q); });
      p->form(makePacketTs(64 * 1024), pool);   // 档2
      p->form(makePacketTs(700 * 1024), pool);  // 换档4: 走 leavePool+acquire
      p.reset();                                // 对象+块归还
    }
  };
  std::vector<std::thread> th;
  for (int32_t t = 0; t < 4; ++t) {
    th.emplace_back(worker);
  }
  for (auto& t : th) {
    t.join();
  }
  // 囤积上限 16, 稳态后新增有限; 不崩、账面不越界即通过
  CHECK(pool->statObjNews() <= 24);
}

}
