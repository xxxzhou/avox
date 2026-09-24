// PtsFlattener 单元测试: 老容器簇状畸形时间戳的摊平
// 数据源为真机采集: build/fzz-probe/pkts.log (E:\备份\方子传CD1.rmvb,
// rv40 声明 24fps 真实约 15fps, 簇内 1ms / 簇基量化在 83ms 栅格)
// 下面嵌入的是该日志前 120 个视频包 pts(倒跳点在第 348 包, 未含), 末尾另有一条
// 读全量日志的用例, 本机有素材时自动跑更大范围
#include <doctest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "video/PtsFlattener.hpp"

namespace avox {

static const int64_t kNominalRv40 = 41;  // 1000/24, 容器声明的帧长

static PacketBufPtr videoPacket(int64_t pts) {
  auto pkt = std::make_shared<PacketBuf>();
  pkt->packtype = (int32_t)PackType::video;
  pkt->pts = pts;
  pkt->dts = pts;
  return pkt;
}

// 复刻 VDecoderTask 取包处的驱动方式: peek 队头喂 pts, 认领才 dequeue,
// 放出的一律经 pop 交付(与接线后的解码线程同一形状)
static std::vector<int64_t> replay(const std::vector<int64_t>& in,
                                   int64_t nominalMs) {
  PtsFlattener flat;
  flat.setup(nominalMs);
  std::vector<PacketBufPtr> queue;
  for (auto pts : in) {
    queue.push_back(videoPacket(pts));
  }
  std::vector<int64_t> out;
  auto drain = [&flat, &out]() {
    while (true) {
      PacketBufPtr got;
      if (!flat.pop([&got](const PacketBufPtr& pkt) { got = pkt; })) {
        return;
      }
      out.push_back(got->pts);
    }
  };
  size_t i = 0;
  while (i < queue.size() || flat.pending()) {
    if (i < queue.size() && flat.feed(queue[i]->pts)) {
      flat.take(queue[i]);
      ++i;
    } else if (i < queue.size()) {
      out.push_back(queue[i]->pts);  // 旁路: 原值直发
      ++i;
    } else {
      flat.finish();  // 输入排空: 尾簇用学到的宽度收尾
    }
    drain();
  }
  return out;
}

static std::vector<int64_t> gaps(const std::vector<int64_t>& s) {
  std::vector<int64_t> g;
  for (size_t i = 1; i < s.size(); ++i) {
    g.push_back(s[i] - s[i - 1]);
  }
  return g;
}

// 容器原值(前 120 包), 摘自 pkts.log, 未改动
static const int64_t kRv40Real[] = {
    0,    83,   84,   167,  168,  292,  293,  417,  418,  708,  709,  710,
    711,  1000, 1001, 1002, 1003, 1250, 1251, 1252, 1253, 1542, 1543, 1544,
    1545, 1750, 1751, 1752, 1753, 2000, 2001, 2002, 2003, 2292, 2293, 2294,
    2295, 2542, 2543, 2544, 2545, 2833, 2834, 2835, 2836, 3083, 3084, 3085,
    3086, 3333, 3334, 3335, 3336, 3625, 3626, 3627, 3628, 3875, 3876, 3877,
    3878, 4167, 4168, 4169, 4170, 4417, 4418, 4419, 4420, 4542, 4543, 4667,
    4668, 4833, 4834, 4917, 4918, 5000, 5001, 5167, 5168, 5292, 5293, 5417,
    5418, 5542, 5543, 5667, 5668, 5833, 5834, 5958, 5959, 6083, 6084, 6208,
    6209, 6333, 6334, 6500, 6501, 6625, 6626, 6750, 6751, 6833, 6834, 6917,
    6918, 7000, 7001};

TEST_CASE("PtsFlattener: 真机 rv40 簇状时间戳被铺平") {
  std::vector<int64_t> in(kRv40Real, kRv40Real + sizeof(kRv40Real) / sizeof(int64_t));
  auto out = replay(in, kNominalRv40);
  auto gi = gaps(in);
  auto go = gaps(out);
  REQUIRE(out.size() == in.size());
  int32_t stallIn = 0;
  for (auto g : gi) {
    stallIn += g < kNominalRv40 / 4 ? 1 : 0;
  }
  CHECK(stallIn > 40);  // 输入确实是挤簇的
  int32_t stallOut = 0;
  for (auto g : go) {
    stallOut += g < kNominalRv40 / 4 ? 1 : 0;
    CHECK(g >= 0);       // 不产生时间倒退
  }
  CHECK(stallOut == 0);
  CHECK(out.front() == in.front());  // 首包不前移
  CHECK(out.back() >= in.back());    // 尾包不落后于容器
}

TEST_CASE("PtsFlattener: 正常恒定帧率流逐包 pts 原值不动") {
  const int64_t steps[] = {33, 40, 41};
  for (auto step : steps) {
    std::vector<int64_t> in;
    for (int32_t i = 0; i < 400; ++i) {
      in.push_back(i * step);
    }
    auto out = replay(in, 33);
    REQUIRE(out.size() == in.size());
    CHECK(out == in);
  }
}

TEST_CASE("PtsFlattener: 同 pts 包对(同帧 NAL 分片)不成簇, 原值直发") {
  // 帧前置 SEI 拆包后与图像包同 pts/dts(11a263f SEI 放行), 旧判据 pts >=
  // 误判成簇摊平, 解码输出帧序两两互换、画面来回跳(换脸片源实证)
  std::vector<int64_t> in;
  for (int32_t i = 0; i < 60; ++i) {
    in.push_back(i * 33);
    in.push_back(i * 33);  // 同帧的 SEI 包
  }
  auto out = replay(in, 33);
  REQUIRE(out.size() == in.size());
  CHECK(out == in);
}

TEST_CASE("PtsFlattener: VFR 抖动流不算挤簇, pts 原值不动") {
  std::vector<int64_t> in;
  int64_t t = 0;
  const int64_t pattern[] = {33, 50, 25, 40};
  for (int32_t i = 0; i < 400; ++i) {
    in.push_back(t);
    t += pattern[i % 4];
  }
  auto out = replay(in, 33);
  CHECK(out == in);
}

TEST_CASE("PtsFlattener: 容器自身倒跳不得输出成时间倒退") {
  // 复刻 pkts.log 第 344~352 包的形态: 簇内末包之后插进一个更早的 pts
  std::vector<int64_t> in = {0,    83,   84,   167,  120,
                             121,  200,  201,  292,  293,
                             400,  401,  500,  501,  502,  503};
  auto out = replay(in, kNominalRv40);
  REQUIRE(out.size() == in.size());
  for (auto g : gaps(out)) {
    CHECK(g >= 0);
  }
}

TEST_CASE("PtsFlattener: 尾簇靠 finish 放出, 不能扣死") {
  std::vector<int64_t> in;
  for (int32_t i = 0; i < 20; ++i) {
    in.push_back(i * 200);
  }
  in.push_back(4000);
  in.push_back(4001);  // 最后这一簇再没有下一簇基来定宽
  auto out = replay(in, kNominalRv40);
  REQUIRE(out.size() == in.size());
  CHECK(out.back() > 4001);          // 尾帧被推到学到的宽度之内, 没被丢掉
  for (auto g : gaps(out)) {
    CHECK(g >= 0);
  }
}

TEST_CASE("PtsFlattener: 标称帧率缺失时整体旁路") {
  PtsFlattener flat;
  flat.setup(0);
  CHECK(!flat.enabled());
  CHECK(!flat.feed(0));   // 不认领任何包
  CHECK(!flat.pending());
  PacketBufPtr got;
  CHECK(!flat.pop([&got](const PacketBufPtr& pkt) { got = pkt; }));
}

TEST_CASE("PtsFlattener: 簇宽容不下时整簇原值直发而不是硬压") {
  // 声明 24fps(nom 41), 一簇 6 帧挤在 1ms 内但下一簇基只给了 100ms:
  // 6*41/2=123 > 100, 铺不开, 必须原样放行
  std::vector<int64_t> in = {0, 1, 2, 3, 4, 5, 100, 200};
  auto out = replay(in, kNominalRv40);
  REQUIRE(out.size() == in.size());
  CHECK(out[0] == 0);
  CHECK(out[5] == 5);  // 前 6 个未被改写
}

TEST_CASE("PtsFlattener: 逐包不丢不重") {
  std::vector<int64_t> in(kRv40Real, kRv40Real + sizeof(kRv40Real) / sizeof(int64_t));
  PtsFlattener flat;
  flat.setup(kNominalRv40);
  int32_t taken = 0;
  int32_t got = 0;
  // 模拟"每轮 feed 一次 + pop 一次"的解码线程: 交付总数必须等于接管总数
  for (size_t i = 0; i < in.size(); ++i) {
    if (flat.feed(in[i])) {
      flat.take(videoPacket(in[i]));
      ++taken;
    }
    PacketBufPtr pkt;
    if (flat.pop([&pkt](const PacketBufPtr& p) { pkt = p; })) {
      ++got;
    }
  }
  flat.finish();
  while (true) {
    PacketBufPtr pkt;
    if (!flat.pop([&pkt](const PacketBufPtr& p) { pkt = p; })) {
      break;
    }
    ++got;
  }
  CHECK(taken == (int32_t)in.size());
  CHECK(got == (int32_t)in.size());
}

TEST_CASE("PtsFlattener: 全量真机日志(素材在位时跑)") {
  // 该用例读 fzz-probe 目录下的 pkts.log; 换机器时路径不存在则跳过
  const char* paths[] = {"pkts.log", "../fzz-probe/pkts.log",
                         "build/fzz-probe/pkts.log"};
  std::vector<int64_t> in;
  for (auto path : paths) {
    std::ifstream lf(path);
    if (!lf.is_open()) {
      continue;
    }
    std::string line;
    while (std::getline(lf, line)) {
      const std::string key = "---video packet pts:";
      auto j = line.find(key);
      if (j == std::string::npos) {
        continue;
      }
      in.push_back(std::atoll(line.c_str() + j + key.size()));
    }
    break;
  }
  if (in.size() < 500) {
    MESSAGE("pkts.log 不在位, 跳过全量用例");
    return;
  }
  auto out = replay(in, kNominalRv40);
  REQUIRE(out.size() == in.size());
  int32_t stall = 0;
  for (auto g : gaps(out)) {
    stall += g < kNominalRv40 / 4 ? 1 : 0;
  }
  CHECK(stall == 0);
}

}
