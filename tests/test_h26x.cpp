// H26XHelper 单元测试: annexb/avcc 头检测 + avcc extradata 组包/拆包往返
#include <doctest.h>

#include <vector>

#include "codec/H26XHelper.hpp"
#include "source/PacketBuf.hpp"

namespace avox {

TEST_CASE("checkAnnexbHeader: 3/4 字节起始码与非法输入") {
  const uint8_t sc3[] = {0x00, 0x00, 0x01, 0x67};
  const uint8_t sc4[] = {0x00, 0x00, 0x00, 0x01, 0x67};
  const uint8_t bad[] = {0x00, 0x00, 0x02, 0x67};

  CHECK(checkAnnexbHeader(sc3, sizeof(sc3)) == 3);
  CHECK(checkAnnexbHeader(sc4, sizeof(sc4)) == 4);
  CHECK(checkAnnexbHeader(bad, sizeof(bad)) == -1);
  CHECK(checkAnnexbHeader(sc3, 2) == -1);  // 长度不足
}

TEST_CASE("checkAvccHeader: lengthSize = (byte[4]&0x03)+1, 仅 1/2/4 合法") {
  // configurationVersion 必须是 0x01
  const uint8_t rec4[] = {0x01, 0x64, 0x00, 0x28, 0xFF};  // (0xFF&3)=3 -> 4
  const uint8_t rec3[] = {0x01, 0x64, 0x00, 0x28, 0xFE};  // -> 3, 非法
  const uint8_t rec2[] = {0x01, 0x64, 0x00, 0x28, 0xFD};  // -> 2
  const uint8_t rec1[] = {0x01, 0x64, 0x00, 0x28, 0xFC};  // -> 1
  const uint8_t badver[] = {0x02, 0x64, 0x00, 0x28, 0xFF};

  CHECK(checkAvccHeader(rec4, sizeof(rec4)) == 4);
  CHECK(checkAvccHeader(rec3, sizeof(rec3)) == -1);
  CHECK(checkAvccHeader(rec2, sizeof(rec2)) == 2);
  CHECK(checkAvccHeader(rec1, sizeof(rec1)) == 1);
  CHECK(checkAvccHeader(badver, sizeof(badver)) == -1);
  CHECK(checkAvccHeader(rec4, 4) == -1);  // 长度不足
}

// 构造一个 annexb 形式的 SPS/PPS 包 (4 字节起始码 + NAL)
static PacketBuf makeAnnexbNal(uint8_t nalHeader, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> raw = {0x00, 0x00, 0x00, 0x01, nalHeader};
  raw.insert(raw.end(), payload.begin(), payload.end());
  PacketBuf p(raw);
  p.prefixSize = 4;
  return p;
}

TEST_CASE("h264CombinExtradata/h264SplitAvcc 往返") {
  // SPS: NAL 头 0x67, profile/compat/level = 64/00/28 (High 4.0)
  PacketBuf sps = makeAnnexbNal(0x67, {0x64, 0x00, 0x28, 0xAA, 0xBB});
  // PPS: NAL 头 0x68
  PacketBuf pps = makeAnnexbNal(0x68, {0x11, 0x22, 0x33});

  std::vector<uint8_t> extradata;
  REQUIRE(h264CombinExtradata({sps, pps}, extradata));

  // 组包布局: ver(01) profile compat level lenSize(FF->4字节) spsCnt(0xE1) ...
  // SPS NAL = 头0x67 + 5字节 payload = 6 字节; PPS NAL = 0x68 + 3字节 = 4 字节
  REQUIRE(extradata.size() == 21);
  CHECK(extradata[0] == 0x01);
  CHECK(extradata[1] == 0x64);  // profile 取自 SPS payload[1]
  CHECK(extradata[2] == 0x00);
  CHECK(extradata[3] == 0x28);
  CHECK(extradata[4] == 0xFF);   // 4 字节 NALU 长度
  CHECK(extradata[5] == 0xE1);   // SPS 数量 1
  CHECK(extradata[6] == 0x00);
  CHECK(extradata[7] == 0x06);   // SPS 长度 6 (不含起始码, 含 NAL 头)
  CHECK(extradata[8] == 0x67);
  CHECK(extradata[13] == 0xBB);  // SPS payload 尾字节
  CHECK(extradata[14] == 0x01);  // PPS 数量 1
  CHECK(extradata[15] == 0x00);
  CHECK(extradata[16] == 0x04);  // PPS 长度 4
  CHECK(extradata[17] == 0x68);
  // 自己写的包要能被自己的 checkAvccHeader 认出 (长度粒度 4)
  CHECK(checkAvccHeader(extradata.data(), (int32_t)extradata.size()) == 4);

  // 拆包: 还原出 SPS/PPS, buff = 4字节大端长度 + NAL, prefixSize=4
  std::vector<PacketBuf> out;
  h264SplitAvcc(extradata.data(), (int32_t)extradata.size(), out);
  REQUIRE(out.size() == 2);
  CHECK(out[0].prefixSize == 4);
  CHECK(out[0].size == 4 + 6);
  CHECK(out[0].buff[4] == 0x67);
  CHECK(out[0].buff[5] == 0x64);
  CHECK(out[0].buff[9] == 0xBB);
  CHECK(out[1].size == 4 + 4);
  CHECK(out[1].buff[4] == 0x68);
  CHECK(out[1].buff[7] == 0x33);
}

}  // namespace avox
