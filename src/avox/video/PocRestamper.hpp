#pragma once

#include <memory>

#include "../AvoxCodec.h"

namespace avox {

class H264Parse;
class H265Parse;

// 丢 ctts 的 B 帧流显示时间戳重建器
// 症状: mp4/转码产物缺 ctts(直播录制重封装常见), pts==dts 按解码序直出,
// B 帧内容在显示序上往复 → 画面来回闪动
class AVOX_EXPORT PocRestamper {
 public:
  PocRestamper();
  ~PocRestamper();

 public:
  // fps<=0 或编码不是 h264/h265 时整体旁路
  void setup(VCodecId codecId, double fps);
  // seek 回跳在 feed 内自愈(dts 明显回跳即重置), 此接口供外部显式重置
  void reset();
  // 帧包/含配置帧的包入口, 就地改写 packet.pts; 返回 true 表示本次有改写
  bool feed(AvoxPacket& packet);
  bool active() const { return mode == Mode::active; }

 private:
  enum class Mode {
    bypass,  // 永久旁路(pts!=dts 的正常流 / 解不出 poc 参数 / 非h26x)
    armed,   // 观察中: pts==dts 但还没见到 POC 倒挂
    active   // 已确认倒挂: 注入合成 ctts
  };
  // 遍历包内 NAL: 配置帧喂解析器, 首个 VCL 解 slice 头取 poc
  // 返回 true 表示本包按单帧处理完成(可记账可改写); 多帧包返回 false
  bool processPacketNals(AvoxPacket& packet, bool& bIdr);
  // POC 记账: lsb 解回绕, IRAP 重置 GOP; 返回 false 表示本帧不可用
  bool updatePoc(int64_t lsb, bool bIrap);
  // 置 active 并按 reason 打一次激活日志
  void activate(const char* reason);

 private:
  VCodecId codecId = VCodecId::none;
  Mode mode = Mode::armed;
  std::unique_ptr<H264Parse> h264Parse;
  std::unique_ptr<H265Parse> h265Parse;
  int64_t frameDurMs = 0;
  int64_t frameDurUs = 0;   // 显示格步长用 µs: ms 整数对非整帧率截断累积漂移
  int64_t pts0Us = 0;       // pts0 的 µs 形式(改写基准)
  int64_t pocStep = 2;      // 相邻显示帧的 lsb 步进: h264=2, h265=1
  int64_t lsbWrap = 512;    // lsb 回绕周期 2^log2_max_pic_order_cnt_lsb
  bool bPocReady = false;   // SPS 已解出(poc 参数可读)
  int64_t fullPoc = 0;      // 解回绕后的全 POC(显示序单调)
  int64_t maxFullPoc = 0;   // 本 GOP 内最大全 POC = 显示序末帧位(IRAP 定位用)
  int64_t prevLsb = 0;
  int64_t fullPoc0 = 0;     // 首帧全 POC(显示位基准)
  int64_t pts0 = 0;         // 首帧原始 pts(改写基准)
  bool bPts0 = false;       // pts0 已锚定
  int64_t lastDts = 0;      // seek 自愈: dts 回跳检测
  bool bAnchor = false;     // 已记账首帧
  bool bLogged = false;     // 激活日志只打一次
};

}
