#pragma once

#include <functional>
#include <vector>

#include "../source/PacketBuf.hpp"

namespace avox {

// 老容器(RMVB/AVI 等)的簇状畸形时间戳摊平器
// 症状: 簇内帧被写成 1~3ms 间隔, 簇基却按声明帧率的栅格量化, 播放器成串扣帧停顿
// 做法: 扣住当前簇, 看到下一簇首包后整簇铺到 [base, base+gap) 的等分格上
// 只在解码取包侧生效: 容器与导出的时间戳保持原值, 修的是呈现节奏
// 恒等性: 单包簇(正常流)位移恒为 0, 逐包原值下发
class AVOX_EXPORT PtsFlattener {
 public:
  PtsFlattener() = default;
  ~PtsFlattener();

 public:
  // nominalMs 标称帧长(来自 VideoDesc.fps), <=0 表示整体旁路
  void setup(int64_t nominalMs);
  // seek / 换解码器 / 轨关闭: 未放出的簇作废, 重新起量
  void reset();
  // 喂入队头包的 pts(不出队); 返回 true 表示摊平器接管这个包, 调用方随后
  // dequeue 并交给 take()。返回 false 只发生在整体旁路(无标称帧率)或配置包。
  // 正常流的单包簇位移恒 0, 即"接管但原值下发", 故每包都走摊平器
  bool feed(int64_t pts);
  // 接管 feed() 认领的那个包(必须紧跟 feed 返回 true 之后调用)
  void take(const PacketBufPtr& packet);
  // 取出一个已放出的包(摊平后的顺序即交付顺序), 无则返回 false
  bool pop(const std::function<void(PacketBufPtr)>& emit);
  // 输入排空(EOF): 尾簇没有下一簇基可量宽, 用学到的簇宽收尾
  void finish();
  // 手里还扣着包: 排空输入后必须 finish, 否则尾簇永远放不出来
  bool pending() const { return !cluster.empty(); }
  // 摊平器仍在生效(且标称帧率可用): 调用方不该把包绕过它直发
  bool enabled() const { return bEnabled; }

 private:
  // 放出当前簇: gap 为两簇基之差(即本簇真实宽度), 只进 ready 不外发
  void flushCluster(int64_t gap);
  // 交付记账: 只记已输出最大 pts, 供摊平簇守单调下界; 直发包一律原值
  void record(const PacketBufPtr& pkt);
  void logSummary();

 private:
  int64_t nominalMs = 0;
  int64_t clusterMs = 0;    // 判簇阈值: 与簇内末包间隔小于它算同簇
  int64_t widthMs = 0;      // 学到的真实簇宽, 尾簇收尾用
  int64_t lastOut = -1;     // 已输出的最大 pts, 跨簇单调下界; -1 表示还没输出过
  bool bEnabled = false;
  bool bCounted = false;    // 本流是否真动过时间戳(只在动过时打一行摘要)
  int32_t nFlat = 0;        // 命中摊平的簇数
  int32_t nSkipped = 0;     // 铺不开而整簇原值直发的簇数
  int32_t nClamped = 0;     // 被单调下界钳住的包数
  std::vector<PacketBufPtr> cluster;  // 扣住的当前簇
  std::vector<PacketBufPtr> ready;    // 已放出待逐个交付的包
};

}
