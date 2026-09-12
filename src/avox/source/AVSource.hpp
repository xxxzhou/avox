#pragma once

#include <deque>
#include <utility>

#include "BaseSource.hpp"

namespace avox {

// I帧历史, 用于检测HLS分片重叠导致的重复GOP
// 服务器相邻TS分片常重叠1~3个GOP, 重叠部分是已播过的完整GOP(I帧起, PTS与SIZE完全一致)
// 只记录最近若干个I帧的(PTS, SIZE), 命中即判定为重复GOP, 从该I帧到下一个新I帧间的包全部丢弃
struct IFramesHistory {
  static constexpr int32_t kMaxSize = 10;
  // (pts, size), I帧间隔通常1~4s, 10个约覆盖10~40s
  std::deque<std::pair<int64_t, int32_t>> items;
  bool has(int64_t pts, int32_t size) const {
    for (auto& it : items) {
      if (it.first == pts && it.second == size) {
        return true;
      }
    }
    return false;
  }
  void add(int64_t pts, int32_t size) {
    if (has(pts, size)) {
      return;
    }
    items.push_back({pts, size});
    if ((int32_t)items.size() > kMaxSize) {
      items.pop_front();
    }
  }
  void clear() { items.clear(); }
};

struct IoPlanDesc {
  // 方案名
  std::string name = "";
  // 检查url是否支持
  bool (*bSupport)(const char* url);
};

struct TrackInfo {
  // 上一个数据包的时间
  int64_t prePts = AVOX_NOVALID_PTS;
  // invalid pts修正后的值, 同一帧的多个NAL共享(连续invalid pts包)
  // 正常帧时重置为AVOX_NOVALID_PTS, 表示不在同一帧内
  int64_t lastRevisedPts = AVOX_NOVALID_PTS;
  // 连续invalid包计数: 前几个包按"同帧NAL"复用时间戳(h264参数集组),
  // 之后视为新帧按帧时长递推合成(全NOPTS流如VCD时代的mpeg-ps)
  int32_t revisedCount = 0;
};

// 音频，视频，字幕信息
struct TrackInfos {
  // 是否关闭(主动)
  // bool bDisable = false;
  // 是否有(IO里是否有)
  int32_t trackSize = 0;
  // 记录流的基准时间
  int64_t basePts = AVOX_NOVALID_PTS;
  // 视频流连续无关键帧标志的包数(容器缺关键帧信息时兜底用)
  int32_t noKeyPackets = 0;
  TrackType type = TrackType::none;
  std::vector<TrackInfo> tracks;

  void reset() { basePts = AVOX_NOVALID_PTS; noKeyPackets = 0; }
  int64_t getPrePts() const {
    if (tracks.empty()) {
      return AVOX_NOVALID_PTS;
    }
    return tracks[0].prePts;
  }
};

// IAVSource回调,需要编解码相关的H264/H265/AAC数据
class IAVSourceOb {
 public:
  IAVSourceOb() = default;
  virtual ~IAVSourceOb() = default;

 public:
  // track解析完成(类似SDP,音频/视频元数据)
  // 在这后才能调用IOParse里的gettrack才能获取正确信息
  virtual void onReady() {};
  // 音频与视频包设置基准后反馈，注意跳变后要重设
  virtual void onSyncPts() {};
  virtual void onClose() {}
  virtual void onComplete() {};
  virtual void onError(AVError error, const char* msg) {};
  virtual void onReopen() {}
  // 网络包解封装后的数据，如H264是Annexb/AVC等格式，音频是acc等格式
  virtual void onPacket(const AvoxPacket& frame) {};
  // I帧模式变化，只有I帧没有P/B帧时为true
  virtual void onIFrameMode(bool bIFrameMode) {};
};

// IAVSource是给外部项目用的，这是项目内部基类
// 特殊情况下，一路IO里可能有多个视频流，多个音频流
// 后续需改进:
// 1. open的是模板参数，不同设备不同的参数
// 2. videoInfo/audioInfo单路与多路需要的信息混在一起
// 3. AVSource需根据源类型(需解码/设备直出)来分子类，二者处理差别大
// 4. VTrackDesc/ATrackDesc做基类，AVSource不同对应不同的子类，因记录信息不同
class AVOX_EXPORT AVSource : public BaseSource,
                            public IAVSource,
                            public Observer<IAVSourceOb> {
 public:
  AVSource();
  virtual ~AVSource();

 protected:
  // 源地址，可能是文件路径，也可能是URL
  std::string url = "";
  // 音频与视频相差能同步的最大值
  int32_t maxSyncAvTime = 5000;
  // 音频与视频是否已经对齐
  bool bSyncPts = false;
  // 重复GOP丢弃中(HLS分片重叠): 从重复I帧到下一个新I帧间的包都丢弃
  bool bDiscardDupGop = false;
  // seek保护期: 子类seekTo置true, 第一个video I帧到达时重置重复检测状态并置false
  // 保护期内不做重复检测, 避免seek后I帧(PTS可能与历史重复, 如回跳到已播位置)被误判
  bool bSeeking = false;
  // I帧(PTS, SIZE)历史, 用于检测HLS分片重叠的重复GOP
  IFramesHistory iFrameHistory;
  // 基准时间,单位毫秒
  int64_t baseTimeMS = AVOX_NOVALID_PTS;
  TrackInfos videoInfo = {};
  TrackInfos audioInfo = {};
  // 默认会根据URL分析源类型,但是以手动设置为主
  AVSourceMode sourceMode = AVSourceMode::none;
  // 视频索引映射表，用于多流时，将视频流映射到指定的索引
  std::vector<int32_t> vIndexMaps;
  // 音频索引映射表，用于多流时，将音频流映射到指定的索引
  std::vector<int32_t> aIndexMaps;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  // I帧/P帧合并包
  std::vector<AvoxPacket> combineBufs;
  // 缓存的配置包
  std::vector<PacketBuf> vconfigPackets;
  // 缓存的配置包
  PacketBufPtr aconfigPacket = nullptr;
  // 检测每个I帧前是否有发送过配置包
  bool bSendConfig = false;
  // 如果有音频与视频
  bool bAVAlign = false;
  // 检查是否annexb/avcc
  bool bCheckAcc = false;
  // 是否是avcc/hvcc包
  bool bvcc = false;
  // 速度
  double speed = 1.0;
  // 快速读取模式(录制场景)：speed>1时IO层非阻塞读取，尽快消费数据
  bool bFastRead = false;
  // 上一个I帧的时间
  int64_t preIFramePts = AVOX_NOVALID_PTS;
  // I帧模式检测：连续不同PTS的I帧数据包只有I帧没有P/B帧
  bool bIFrameMode = false;
  int32_t iFrameCount = 0;
  int64_t lastIFramePts = AVOX_NOVALID_PTS;

  // 配置
  // IO超时时间，毫秒
  int32_t timeoutMs = 8000;
  // Track ready等待超时，毫秒(只有1个Track时等第二个Track来的超时)
  int32_t trackReadyMs = 3000;
  std::string rtspTransport = "tcp";
  bool bLogPacket = false;

 public:
  // 源类型
  AVSourceMode getSourceMode() const { return sourceMode; }
  // 如果是服务器变速，可能要在open之前通知服务器
  void setSpeed(double speed);
  // 启用快速读取模式(录制场景)，speed>1时IO层非阻塞读取尽快消费数据
  void setFastRead(bool fast) { bFastRead = fast; }
  // 获取丢包率 (仅 RTSP/RTP 等 UDP 协议有效, 默认返回 0)
  virtual float getLossRate(TrackType type) { return 0.0f; }

 public:
  void processPacket(AvoxPacket& packet);
  // 视频包的处理
  // 1. 配置帧与I帧合并在一个包需要分开
  // 2. 多个同PTS的I帧合并
  void processVideo(AvoxPacket& packet);
  void singleVideo(AvoxPacket& packet);
  // 对齐包时间
  // 1. 修正异常PTS(如FFmpeg拉HLS出现的垃圾值), 用prePts+1代替, 同帧NAL共享修正值
  // 2. 检测HLS分片重叠的重复GOP, 命中则丢弃(不进入后续基准/同步逻辑, prePts不更新)
  // 3. 如果有视频, 以第一个I帧为基准; 如果有音频没有视频, 以第一个音频包为基准
  // 4. 音视频基准都确定后, 如果差值<5s则统一以视频基准对齐; 否则各播各的
  // 5. 检测PTS跳变(>5s), 跳变时重置基准时间并重新同步
  void alignPacketPts(AvoxPacket& packet);
  // 修正异常PTS, 返回true表示已修正
  bool reviseInvalidPts(AvoxPacket& packet, TrackInfos& info);
  // 检查是否有跳变，有跳变则重新设置基准时间
  void checkJump(AvoxPacket& packet);
  // 检测并丢弃HLS分片重叠导致的重复GOP, 返回true表示该包应丢弃
  // seek保护期内不做检测; 命中已记录的I帧(PTS,SIZE)则进入丢弃模式
  bool checkDupGop(AvoxPacket& packet);
  // 当前播放PTS，绝对时间
  int64_t getNowPts() const;
  // IO包的基准时间，单位毫秒
  int64_t getBaseTime(TrackType type = TrackType::none);
  // 音视频当前prePts差值是否在同步容差内
  bool checkAvSynced() const;

 public:
  // 返回相对起始时间的播放时间，单位毫秒
  virtual int64_t position() const override;
  void updateConfig(const AvoxPacket& data);

 public:
  // 初始化，打开文件/网络流
  virtual bool open(const char* url) override;
  // 关闭
  virtual void close() override;
  //
  virtual ISourceInfo* getSourceInfo() override;
  // 子类得到track信息后调用，初始化PTS对齐状态值
  virtual void onTrackOpen() override;

 public:
  virtual void onOptionChange(const char* key, ArgType option) override;

  // 子类具体实现
 public:
  virtual bool onOpen() { return true; };
  virtual void onClose() {};
  // 检查IO线程是否还在运行
  virtual bool vaild() { return bOpen; };
  virtual void onSpeed() {};
  // seek 前预通知: MediaPlayer 撤背压(pauseIOPacket)的同时调用, 让 IO 线程提前打断
  // av_read_frame. 若等到 seekTo 才打断, 快源读线程已失背压狂奔到 EOF → 假 EOF 卡死
  virtual void preSeek() {};
};

}
