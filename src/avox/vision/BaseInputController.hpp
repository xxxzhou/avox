#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "avox/AvoxInput.h"

namespace avox {

// IInputController 的核心基类: 组合输入接口 + 拟人化基础设施 (RNG / 贝塞尔轨迹 / 抖动)。
// 平台实现 (WinInputController / NoopInputController) 继承它, 复用拟人化机制。
// 拟人化配置 setter 不进公共 IInputController (保持公共接口最小, 跨 DLL 安全),
// 经本基类暴露, 调用方 dynamic_cast<BaseInputController*> 设置 (同 addTextRecognizerOb cross-cast)。
// 范本: BaseTextRecognizer (组合 ITextRecognizer + Observer)。
class BaseInputController : public IInputController {
 public:
  // 构造时用随机设备播种 (默认不可复现); setJitterSeed(>0) 可换成固定种子做可复现回放
  BaseInputController();
  virtual ~BaseInputController() = default;

 protected:
  bool humanize = false;          // 拟人化总开关, 默认关 (线性/瞬时, 行为不变)
  int32_t clickHoldMs = 0;        // 点击按住: 0=humanize 时随机 40~90ms; >0=固定
  std::mt19937 rng;               // 互斥由子类 mtx 保护 (这些方法只在 *Impl 里调, *Impl 不上锁, 调用方 public 方法已 hold lock)
  // baseMs ± 30% 随机抖动, 下限 1ms; humanize 关闭时原样返回
  int32_t jitterMs(int32_t baseMs);
  // 随机点击按住时长: clickHoldMs>0 用固定值, 否则 (humanize) 随机 40~90ms, 否则 0
  int32_t clickHold();
  // 三次贝塞尔采样: P0..P3 上取 steps 个点 (不含 P0, 含 P3, 终点精确), 追加到 out
  void sampleBezier(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                    int32_t steps, std::vector<std::pair<int32_t, int32_t>>& out);
  // 随机生成 2 个控制点: P1@路径 1/3, P2@2/3, 沿垂直方向偏移 10~30% 距离, clamp 到 [bx,bx+bw]×[by,by+bh]
  void makeControlPoints(int32_t x0, int32_t y0, int32_t x3, int32_t y3,
                         int32_t bx, int32_t by, int32_t bw, int32_t bh,
                         int32_t& cx1, int32_t& cy1, int32_t& cx2, int32_t& cy2);

 public:
  // 拟人化总开关, 默认 false; true 启用贝塞尔轨迹 + 自然点击保持 + 抖动延迟
  void setHumanize(bool on);
  // 抖动种子: 0=忽略(沿用构造随机种子); >0=固定种子 (轨迹/时序可复现, 便于回放/压测)
  void setJitterSeed(uint32_t seed);
  // 点击按住时长: 0=humanize 时随机; >0=固定毫秒。humanize 关闭时忽略
  void setClickHoldMs(int32_t ms);
};

}
