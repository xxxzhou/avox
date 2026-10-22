#include "PacketBufPool.hpp"

#include "PacketBuf.hpp"

namespace avox {

PacketBufPool::PacketBufPool() = default;

PacketBufPool::~PacketBufPool() {
  // 囤积的 PacketBuf 已 recycle 过(块已交还、字段清零), 直接 delete 本体
  for (PacketBuf* p : objFree) {
    delete p;
  }
  objFree.clear();
}

PacketBuf* PacketBufPool::objAcquire() {
  std::lock_guard<std::mutex> lock(mtx);
  if (objFree.empty()) {
    nObjNew.fetch_add(1, std::memory_order_relaxed);
    return new PacketBuf();
  }
  nObjHit.fetch_add(1, std::memory_order_relaxed);
  PacketBuf* p = objFree.back();
  objFree.pop_back();
  return p;
}

void PacketBufPool::objRelease(PacketBuf* p) {
  if (!p) {
    return;
  }
  p->recycle();  // 块交回块池, 字段清零
  std::lock_guard<std::mutex> lock(mtx);
  if ((int32_t)objFree.size() < kMaxObjFree) {
    objFree.push_back(p);
    return;
  }
  delete p;  // 超囤积上限: 本体直接释放
}

int32_t PacketBufPool::classOf(int32_t size) {
  for (int32_t i = 0; i < kClassCount; ++i) {
    if (size <= kClassBytes[i]) {
      return i;
    }
  }
  // 超过最大档(8M, 4K I 帧上限够用): 不入池, 走普通分配
  return -1;
}

void PacketBufPool::acquire(int32_t cls, int32_t need, std::vector<uint8_t>& out) {
  nAcquire.fetch_add(1, std::memory_order_relaxed);
  const int32_t statCls = (cls >= 0 && cls < kClassCount) ? cls : kClassCount;
  if (cls < 0 || cls >= kClassCount) {
    // 超最大档: 不入池, 普通分配
    nBlockNew.fetch_add(1, std::memory_order_relaxed);
    clsNew[statCls].fetch_add(1, std::memory_order_relaxed);
    out.reserve(need);
    out.resize(0);
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  auto& list = freeList[cls];
  // 同档里挑装得下的最小块, 避免小块占大坑
  int32_t pick = -1;
  size_t best = size_t(-1);
  for (int32_t i = 0; i < (int32_t)list.size(); ++i) {
    const size_t cap = list[i].capacity();
    if (cap >= (size_t)need && cap < best) {
      best = cap;
      pick = i;
    }
  }
  if (pick < 0) {
    // 池里没有: 按实际尺寸新建, 不按档位上界(4K IDR 3M 就只占 3M)
    nBlockNew.fetch_add(1, std::memory_order_relaxed);
    clsNew[statCls].fetch_add(1, std::memory_order_relaxed);
    out = std::vector<uint8_t>();
    out.reserve(need);
    liveBytes += (int64_t)out.capacity();
    if (liveBytes > nPeakBytes.load(std::memory_order_relaxed)) {
      nPeakBytes.store(liveBytes, std::memory_order_relaxed);
    }
    return;
  }
  nHit.fetch_add(1, std::memory_order_relaxed);
  clsHit[statCls].fetch_add(1, std::memory_order_relaxed);
  out = std::move(list[pick]);
  list.erase(list.begin() + pick);
  bytesHeld -= (int64_t)out.capacity();
  out.resize(0);
}

void PacketBufPool::release(int32_t cls, std::vector<uint8_t>& buf) {
  if (cls < 0 || cls >= kClassCount) {
    std::vector<uint8_t>().swap(buf);
    return;
  }
  const int64_t cap = (int64_t)buf.capacity();
  if (cap == 0) {
    return;
  }
  buf.resize(0);
  std::lock_guard<std::mutex> lock(mtx);
  liveBytes -= cap;
  if (liveBytes < 0) {
    // append 等路径导致块被 vector 重分配时账面对不上, 兜底归零
    liveBytes = 0;
  }
  auto& list = freeList[cls];
  if ((int32_t)list.size() >= kMaxPerClass || bytesHeld + cap > kMaxBytes) {
    // 超配额: 让 buf 随调用方作用域释放, 池不囤积
    return;
  }
  bytesHeld += cap;
  list.push_back(std::move(buf));
}

int64_t PacketBufPool::heldBytes() {
  std::lock_guard<std::mutex> lock(mtx);
  return bytesHeld;
}

void PacketBufPool::reset() {
  std::lock_guard<std::mutex> lock(mtx);
  for (int32_t i = 0; i < kClassCount; ++i) {
    freeList[i].clear();
  }
  // 囤积对象已 recycle 过(字段清零、块已还), 直接 delete 本体
  for (PacketBuf* p : objFree) {
    delete p;
  }
  objFree.clear();
  bytesHeld = 0;
  liveBytes = 0;
}

}
