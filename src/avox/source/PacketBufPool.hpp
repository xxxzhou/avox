#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace avox {

class PacketBuf;

// 包缓冲池: 按容量档复用, 稳态下不再 malloc
// 档位由本次尺寸决定, 小包永远拿小档 —— 大 I 帧不会把它撑大
// 同时复用 PacketBuf 对象本身(acquirePacket/releasePacket): 槽回绕目标恒为
// "刚出队的包"(必然还在解码管线里被扣着), 原地复用天然轮不到, 对象池补上这块
class PacketBufPool {
 public:
  PacketBufPool();
  ~PacketBufPool();
  PacketBufPool(const PacketBufPool&) = delete;
  PacketBufPool& operator=(const PacketBufPool&) = delete;

 public:
  // 请求尺寸落在哪一档, -1 表示超出最大档(不入池)
  static int32_t classOf(int32_t size);
  // 取一块容量 >= need 的缓冲(逻辑长度归零); 池里没有合适的就新建, 必成功
  void acquire(int32_t cls, int32_t need, std::vector<uint8_t>& out);
  // 按档位归还(缓冲被移走); cls<0 或超上限直接丢弃
  void release(int32_t cls, std::vector<uint8_t>& buf);
  // 池里当前占用的字节数(调试/单测用)
  int64_t heldBytes();
  // 清囤积(空闲块+空闲对象+账面), 在飞块不动(由旧包死亡自然回流); 计数保留
  void reset();

 public:
  // 验收统计(原子, 常驻): 取块/命中/新建、槽对象复用/新建、在飞块字节峰值
  // copyBuf 记槽对象复用, AVTrack 关轨时汇总打一行
  int64_t statAcquires() const { return nAcquire.load(std::memory_order_relaxed); }
  int64_t statHits() const { return nHit.load(std::memory_order_relaxed); }
  int64_t statBlockNews() const { return nBlockNew.load(std::memory_order_relaxed); }
  int64_t statSlotReuses() const { return nSlotReuse.load(std::memory_order_relaxed); }
  int64_t statSlotNews() const { return nSlotNew.load(std::memory_order_relaxed); }
  int64_t statPeakBytes() const { return nPeakBytes.load(std::memory_order_relaxed); }
  // 分档命中/新建: 下标 0..classCount()-1 为各档, classCount() 为未入池(超 8M)
  static constexpr int32_t classCount() { return kClassCount + 1; }
  int64_t statClassHit(int32_t cls) const {
    return clsHit[cls].load(std::memory_order_relaxed);
  }
  int64_t statClassNew(int32_t cls) const {
    return clsNew[cls].load(std::memory_order_relaxed);
  }
  void statSlot(bool bReuse) {
    if (bReuse) {
      nSlotReuse.fetch_add(1, std::memory_order_relaxed);
    } else {
      nSlotNew.fetch_add(1, std::memory_order_relaxed);
    }
  }
  // PacketBuf 对象本身的复用: 取出(字段已清零, 调用方 form 重填)/归还(recycle 后
  // 囤积, 超上限直接 delete)。池析构时统一 delete 囤积对象
  PacketBuf* objAcquire();
  void objRelease(PacketBuf* p);
  int64_t statObjHits() const { return nObjHit.load(std::memory_order_relaxed); }
  int64_t statObjNews() const { return nObjNew.load(std::memory_order_relaxed); }

 private:
  static constexpr int32_t kClassCount = 6;
  // 档位边界: 音频(几 K) / 普通 P 帧(几十 K) / 1080p I 帧 / 4K I 帧
  static constexpr int32_t kClassBytes[kClassCount] = {
      8 * 1024,    32 * 1024,   128 * 1024,
      512 * 1024,  2 * 1024 * 1024, 8 * 1024 * 1024};
  // 每档空闲块囤积上限(注意: 不是在飞块上限! 在飞块归 PacketBuf 所有, 池不设限)。
  // 只在"批量归还"(seek 清队列/丢簇)时生效: 最多留 8 块待复用, 其余直接 free;
  // 常规回绕是"还一块→紧接着取一块", 恒命中, 与该值无关
  static constexpr int32_t kMaxPerClass = 8;
  // 池总预算: 超出就不再囤积, 让多余块自然释放
  static constexpr int64_t kMaxBytes = 32ll * 1024 * 1024;

  std::mutex mtx;
  std::vector<std::vector<uint8_t>> freeList[kClassCount];
  int64_t bytesHeld = 0;
  // 在飞块字节数(估算): acquire 新建时累加, release 时扣回; 只在 mtx 内改
  int64_t liveBytes = 0;
  // 统计
  std::atomic<int64_t> nAcquire{0};
  std::atomic<int64_t> nHit{0};
  std::atomic<int64_t> nBlockNew{0};
  std::atomic<int64_t> nSlotReuse{0};
  std::atomic<int64_t> nSlotNew{0};
  std::atomic<int64_t> nPeakBytes{0};
  std::atomic<int64_t> nObjHit{0};
  std::atomic<int64_t> nObjNew{0};
  // 分档命中/新建(kClassCount 下标 = 未入池)
  std::atomic<int64_t> clsHit[kClassCount + 1]{};
  std::atomic<int64_t> clsNew[kClassCount + 1]{};
  // PacketBuf 对象空闲表(裸指针, 池析构时统一 delete)
  std::vector<PacketBuf*> objFree;
  static constexpr int32_t kMaxObjFree = 16;
};

}
