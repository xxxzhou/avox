// BaseInputController 拟人化基础设施实现: 构造/RNG/贝塞尔/抖动。
// 声明见 BaseInputController.hpp。这些方法只在子类 *Impl (不加锁) 里调,
// 而 *Impl 的调用方 (public 方法) 已 hold mtx, 所以 rng 访问线程安全。

#include "BaseInputController.hpp"

namespace avox {

BaseInputController::BaseInputController() { rng.seed(std::random_device{}()); }

int32_t BaseInputController::jitterMs(int32_t baseMs) {
  if (!humanize || baseMs <= 0) return baseMs;
  std::uniform_int_distribution<int32_t> d(baseMs * 7 / 10, baseMs * 13 / 10);
  int32_t v = d(rng);
  return v < 1 ? 1 : v;
}

int32_t BaseInputController::clickHold() {
  if (!humanize) return 0;
  if (clickHoldMs > 0) return clickHoldMs;
  std::uniform_int_distribution<int32_t> d(40, 90);
  return d(rng);
}

void BaseInputController::sampleBezier(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                       int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                                       int32_t steps, std::vector<std::pair<int32_t, int32_t>>& out) {
  if (steps <= 0) return;
  for (int32_t i = 1; i <= steps; ++i) {
    double t = (double)i / steps;
    double u = 1.0 - t;
    double b0 = u * u * u;
    double b1 = 3.0 * u * u * t;
    double b2 = 3.0 * u * t * t;
    double b3 = t * t * t;
    int32_t x = (int32_t)(b0 * x0 + b1 * x1 + b2 * x2 + b3 * x3);
    int32_t y = (int32_t)(b0 * y0 + b1 * y1 + b2 * y2 + b3 * y3);
    out.emplace_back(x, y);
  }
}

void BaseInputController::makeControlPoints(int32_t x0, int32_t y0, int32_t x3, int32_t y3,
                                            int32_t bx, int32_t by, int32_t bw, int32_t bh,
                                            int32_t& cx1, int32_t& cy1, int32_t& cx2, int32_t& cy2) {
  int32_t dx = x3 - x0;
  int32_t dy = y3 - y0;
  double len = std::sqrt((double)dx * dx + (double)dy * dy);
  double nx, ny;
  if (len < 1.0) {
    // 起止重合: 随机取方向
    double a = std::uniform_real_distribution<double>(0.0, 6.2831853)(rng);
    nx = std::cos(a);
    ny = std::sin(a);
  } else {
    nx = -dy / len;  // 垂直向量
    ny = dx / len;
  }
  std::uniform_real_distribution<double> sign(-1.0, 1.0);  // 偏移方向 (可左可右)
  std::uniform_real_distribution<double> mag(0.10, 0.30);  // 幅度 10~30% 距离
  auto clamp = [](int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); };
  double off1 = sign(rng) * mag(rng) * len;
  double off2 = sign(rng) * mag(rng) * len;
  cx1 = clamp(x0 + dx / 3 + (int32_t)(nx * off1), bx, bx + bw);
  cy1 = clamp(y0 + dy / 3 + (int32_t)(ny * off1), by, by + bh);
  cx2 = clamp(x0 + 2 * dx / 3 + (int32_t)(nx * off2), bx, bx + bw);
  cy2 = clamp(y0 + 2 * dy / 3 + (int32_t)(ny * off2), by, by + bh);
}

void BaseInputController::setHumanize(bool on) { humanize = on; }
void BaseInputController::setJitterSeed(uint32_t seed) {
  if (seed > 0) rng.seed(seed);
}
void BaseInputController::setClickHoldMs(int32_t ms) { clickHoldMs = ms; }

}