// PocRestamper 单元测试: 丢 ctts 的 B 帧流 pts 重建
// fixture 为真实片源前 16 个样本(avoxt 台妹子/91tims 系列, h264 720p30):
// POC 序 0,2,4,6,10,8,18,14,12,16,20,22,32,26,24,28 —— 解码序倒挂实证;
// 每包截断到 NAL 头后 76B 并修正 avcc 长度前缀(slice 头解析只需头部字节)
// 期望表: armed 阶段原值(s0~s4), s5(POC 倒挂帧)激活后按显示格平移
#include <doctest.h>

#include <cstdint>
#include <vector>

#include "video/PocRestamper.hpp"

namespace avox {

namespace {

// 原始样本(avcc 4字节长度前缀)
static const char* kFixture[] = {
    "0000001b67640032ac7204405005bb0110000003001000000303c8f18318460000000768e8438232c8b00000003c658881000dfffef6aefccb2b747e952e1d597bb351f2e84972fd898eb4a8eaabd5e6d2a8a7aed4dfe60000030000030000030000030000030216e898",
    "0000002c419a082d88bffe384000000300000300000300000300000300000300029fdd6ddf201caf8f16450c80007f80",
    "00000028419a104bf02064ca6113fffdf10000030000030000030000030000030000030000030000030006f5",
    "0000004c4188861827fff9412888affc1777b11ffc5456230854c191aef9a9edf92e729ac40c7dac519fd1f92b24f0e1b06a91029de0038f9bece9bcb1484225aff1b1792230fefb9a3241aaa370d705",
    "0000004c419a20a97c081f9329804bfffcd84e0126fe5ecbe7b72ff1eeb39836c5b2beb3ad4c921eff473c9ee1ac9fa25319270d380000030000030000030000065abba968ef92a01e76cbc319a6c8a4",
    "0000004c019e28849212ff6da2166711af7df0000003000003000003000207af8915a963c974ddd1656a6936e3564df98bb6d1eea8853531deb287bf6c62ecce3ab45f0e57a144a0dcd31a446a0d1f16",
    "0000004c419a2929bc081fe4ca6004bf6ec3bc1faddaff4aec0c96c25952653fbcfd84b03bcd52dbef433a64e2126588e596f20fe29a0326d9e692453a491854a5ddb1f709c89bd4221078f859cd418b",
    "0000004c419e30e4b1097f9ea578aab5a88403108d006f80703306426af6384ca60a8548305a995f8d927c60472387903209e6241c7db9eb46dd5d7ac9f3f8102326e4cdc408dabfc4f78f23e979bd36",
    "0000004c019e38c4a884bfa89de17eb2b0976db5e729002c4fab6e574039800285044b2fd877a7c1cadb8dd85f8bfc1dddebc37d52c3d4e12741faeb1e0124ddc69e2dbc48cc8a09241e111be48573f4",
    "0000004c019e390cd212ffce028a4699109d33c396de876eaa75fe989b41c3b59f1b817605f79cdf49a0eb7fc85600137c295c921c70c102f026e1245d0f108da264583b7dd1855a884f077e8742d8b8",
    "0000004c419a39488d4080b5fe4ca60004bf6ea439be1cea6e35aa4a2be140429f366945bea577e08aa5fa3dcfa28584116b70c48f1ff3222f1f1bec0fea1e3c7c55adcd98370373c77ffd2e0ef543b6",
    "0000004c419a41689f02052d7f932980004bffe930815dc493480d6484f82e550f9ff3dceb59f5ff2bee37e3ce2f1282c2a0043ccd04d78dcc7e17a0c979ae6fa315d7a78a918620865793349254818c",
    "0000004c419a4a08af02074b5fe4ca600004bf6ea42f5e3c4c1c0ba9608d53f7eb8701a6e2572afbb1554f0325616ff40a9f4786cae8dc79626904d327b0e971bec9775501a7fe08d5feeb62dc1c3854",
    "0000004c419e51a44c477f4ecd84731cc0788534d00bcf74043b2a0482aafd5e8f4fd000335dc202995c77880089aa9fb52aa9b091a4da7e7696f9f8f1436a8bcd71d4b49ccb9068f0896fa058f723d9",
    "0000004c019e59844a212fcdd4804e363dcf6e4480a985a15e0211134cea3124e158735c594e1d67c5359450260e6b31b7e36ed1e4a0000150fffe03a38ffbe130fcfbd40004b1eaffe44e0644fa5500",
    "0000004c019e59cc5484bfcdfcf7aceac54de7b3972fe86c8ee9f91f2497b8f688c98f800000030003a8928558f581cf38007840d008a24f68d4b2a298b79c4c71fbe0000003000018515938c619ec56",
};

static std::vector<uint8_t> fromHex(const char* hex) {
  std::vector<uint8_t> out;
  for (const char* p = hex; p[0] && p[1]; p += 2) {
    auto nib = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
      if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
      return (uint8_t)(c - 'A' + 10);
    };
    out.push_back((uint8_t)((nib(p[0]) << 4) | nib(p[1])));
  }
  return out;
}

// 构造 pts==dts 的帧包(存储由调用侧的 Storage 保活)
struct Storage {
  std::vector<std::vector<uint8_t>> bytes;
  std::vector<AvoxPacket> packets;
};

static Storage buildFixture(int64_t ptsBase, int64_t dtsBase, int64_t step) {
  Storage st;
  const int32_t n = (int32_t)(sizeof(kFixture) / sizeof(kFixture[0]));
  for (int32_t i = 0; i < n; ++i) {
    st.bytes.push_back(fromHex(kFixture[i]));
    AvoxPacket pkt;
    pkt.packtype = (int32_t)PackType::video;
    pkt.prefixSize = 4;
    pkt.pts = ptsBase + i * step;
    pkt.dts = dtsBase + i * step;
    pkt.data = {st.bytes.back().data(), (int32_t)st.bytes.back().size(), true};
    st.packets.push_back(pkt);
  }
  return st;
}

}  // namespace

TEST_CASE("PocRestamper: 丢ctts的B帧流按POC重建显示时间戳") {
  PocRestamper rst;
  rst.setup(VCodecId::h264, 30.0);
  Storage st = buildFixture(0, 0, 33);
  // 期望: s0~s4 armed 原值; s5(POC 10→8 倒挂)激活, 之后按显示格
  static const int64_t kExpect[] = {0, 33, 66, 99, 133, 132, 297, 231,
                                    198, 264, 330, 363, 528, 429, 396, 462};
  bool bActivated = false;
  for (int32_t i = 0; i < (int32_t)st.packets.size(); ++i) {
    rst.feed(st.packets[i]);
    if (rst.active()) {
      bActivated = true;
    }
    CHECK_EQ(st.packets[i].pts, kExpect[i]);
    CHECK_EQ(st.packets[i].dts, i * 33);  // dts 恒不动
  }
  CHECK(bActivated);
}

TEST_CASE("PocRestamper: 正常容器(pts!=dts)永久旁路零改写") {
  PocRestamper rst;
  rst.setup(VCodecId::h264, 30.0);
  Storage st = buildFixture(7, 0, 33);  // pts 与 dts 恒差 7ms
  for (auto& pkt : st.packets) {
    rst.feed(pkt);
    CHECK_FALSE(rst.active());
  }
  for (int32_t i = 0; i < (int32_t)st.packets.size(); ++i) {
    CHECK_EQ(st.packets[i].pts, 7 + i * 33);
    CHECK_EQ(st.packets[i].dts, i * 33);
  }
}

TEST_CASE("PocRestamper: 无重排的pts==dts流零触碰") {
  PocRestamper rst;
  rst.setup(VCodecId::h264, 30.0);
  // 只喂 POC 单调的 s0~s3(0,2,4,6), 永不见倒挂
  Storage st = buildFixture(0, 0, 33);
  for (int32_t i = 0; i < 4; ++i) {
    rst.feed(st.packets[i]);
    CHECK_EQ(st.packets[i].pts, i * 33);
    CHECK_EQ(st.packets[i].dts, i * 33);
  }
  CHECK_FALSE(rst.active());
}

TEST_CASE("PocRestamper: fps无效旁路") {
  PocRestamper rst;
  rst.setup(VCodecId::h264, 0.0);
  Storage st = buildFixture(0, 0, 33);
  for (auto& pkt : st.packets) {
    rst.feed(pkt);
  }
  for (int32_t i = 0; i < (int32_t)st.packets.size(); ++i) {
    CHECK_EQ(st.packets[i].pts, i * 33);
  }
}

TEST_CASE("PocRestamper: seek回跳自愈重置记账") {
  PocRestamper rst;
  rst.setup(VCodecId::h264, 30.0);
  Storage st = buildFixture(0, 0, 33);
  for (int32_t i = 0; i < 8; ++i) {
    rst.feed(st.packets[i]);
  }
  // 模拟 seek 重入: 从头再喂, dts 回跳远超 2 帧, 记账重置后表现一致
  static const int64_t kExpect8[] = {0, 33, 66, 99, 133, 132, 297, 231};
  Storage st2 = buildFixture(0, 0, 33);
  for (int32_t i = 0; i < 8; ++i) {
    rst.feed(st2.packets[i]);
    CHECK_EQ(st2.packets[i].pts, kExpect8[i]);
  }
}

}
