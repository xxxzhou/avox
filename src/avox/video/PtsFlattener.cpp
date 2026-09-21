#include "PtsFlattener.hpp"

#include "../module/LogHelper.hpp"

namespace avox {

// 簇内包数上限: 超过说明这根本不是"挤簇"(如 I 帧模式/无时间信息流), 不再扣包
static const int32_t kMaxCluster = 16;

PtsFlattener::~PtsFlattener() {
  if (bCounted) {
    logSummary();
  }
}

void PtsFlattener::setup(int64_t nominalMs) {
  // 判簇阈值取 nom/4: 取 nom/2 会把真实帧率略高于声明的流误判成挤簇
  bEnabled = nominalMs > 0;
  this->nominalMs = nominalMs;
  clusterMs = nominalMs / 4;
}

void PtsFlattener::reset() {
  cluster.clear();
  ready.clear();
  widthMs = 0;
  lastOut = -1;
  bCounted = false;
  nFlat = 0;
  nSkipped = 0;
  nClamped = 0;
}

bool PtsFlattener::feed(int64_t pts) {
  if (!bEnabled) {
    return false;
  }
  // 契约: 调用方每轮都要 pop() 一次, 到这里 ready 必已排空(在此丢包即丢帧)
  if (!cluster.empty()) {
    if (pts - cluster.back()->pts < clusterMs &&
        (int32_t)cluster.size() < kMaxCluster) {
      return true;  // 同簇: 继续扣
    }
    // 新簇首包露出, 两簇基之差即上一簇的真实宽度(容器倒跳会给出非正 gap,
    // 自然落到"铺不开"分支原值直发)
    flushCluster(pts - cluster.front()->pts);
  }
  return true;  // 起新簇
}

void PtsFlattener::take(const PacketBufPtr& packet) {
  if (!bEnabled || !packet) {
    return;
  }
  cluster.push_back(packet);
}

bool PtsFlattener::pop(const std::function<void(PacketBufPtr)>& emit) {
  if (ready.empty()) {
    return false;
  }
  const PacketBufPtr& pkt = ready.front();
  record(pkt);
  emit(pkt);
  ready.erase(ready.begin());
  return true;
}

void PtsFlattener::finish() {
  if (cluster.empty()) {
    return;
  }
  // 尾簇没有下一簇基可量宽: 用学到的簇宽, 没有则按包数 * 标称帧长
  const int64_t gap = widthMs > 0 ? widthMs
                                  : (int64_t)cluster.size() * nominalMs;
  flushCluster(gap);
}

void PtsFlattener::flushCluster(int64_t gap) {
  const int32_t n = (int32_t)cluster.size();
  if (n == 0) {
    return;
  }
  const int64_t base = cluster.front()->pts;
  // 摊平闸: 簇宽得容得下 n 个帧(nom/2 的松弛量, 真数据实测取 nom 会漏簇),
  // 铺不开就整簇原值直发, 不硬压时间轴
  const bool bFlat = (n > 1) && (gap * 2 >= (int64_t)n * nominalMs);
  if (bFlat) {
    widthMs = gap;
    nFlat++;
  } else if (n > 1) {
    nSkipped++;
  }
  for (int32_t i = 0; i < n; ++i) {
    // 不摊平时位移恒 0(原值直发), 摊平时把第 i 帧挪到等分格 base + i*gap/n
    const int64_t shift =
        bFlat ? (base + (gap * i) / n - cluster[i]->pts) : 0;
    if (shift != 0) {
      cluster[i]->pts += shift;
      // pts 与 dts 同偏移: 保解码顺序与两者相对关系(RMVB 无 B 帧, 兼容同类 AVI)
      cluster[i]->dts += shift;
      bCounted = true;
    }
    ready.push_back(std::move(cluster[i]));
  }
  cluster.clear();
}

void PtsFlattener::record(const PacketBufPtr& pkt) {
  if (lastOut >= 0 && pkt->pts < lastOut) {
    // 单调下界: 容器时间戳本身可能倒跳(实测样本里有一处 -544ms), 任何输出
    // 不允许落到已输出点之下。钳到 lastOut 而非 +1: 同 pts 两帧是合法的
    pkt->pts = lastOut;
    nClamped++;
    bCounted = true;
  }
  lastOut = pkt->pts;
}

void PtsFlattener::logSummary() {
  log(LogLevel::info, "pts flatten: flat=", nFlat, " skipped=", nSkipped,
      " clamped=", nClamped, " learnedWidth=", widthMs,
      " nominal=", nominalMs);
}

}
