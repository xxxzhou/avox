#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "../module/Ringbuffer.hpp"
#include "../module/RunTask.hpp"
#include "MPCommon.hpp"

// [爱奇艺埋点投递治理实践](https://zhuanlan.zhihu.com/p/421829961)

// 播放器埋点
// 埋点的目的是为了统计用户的行为，比如播放时长，播放进度，播放错误等。

// 如下所有记录要可以序列化与反序列化的，方便后期用于统计分析

namespace avox {

struct MPPingback {
public:
  MPPingback() = default;
  virtual ~MPPingback() = default;

public:
  MPPBType type = MPPBType::none;
  // 埋点时间戳
  Timespan timespan = {};
  // 播放器唯一标识
  int32_t mpid = 0;

protected:
  Json json = {};

public:
  std::string toJson() {
    json["type"] = (int64_t)type;
    json["timespan"] = timespan.ticks;
    json["mpid"] = (int64_t)mpid;
    onToJson(json["data"]);
    return json.dump();
  }

protected:
  virtual void onToJson(Json &json) = 0;
  virtual bool onFromJson(Json &json) { return false; };

public:
  virtual void logPB() = 0;
};

using MPPingbackPtr = std::shared_ptr<MPPingback>;

template <MPPBType T> struct MPPingbackDataMap {
  using Type = void;
};

// 用宏生成特化版本（新增PBCommon基类约束）
#define XX(name, value, str, classtype, tick)                                  \
  template <> struct MPPingbackDataMap<MPPBType::name> {                       \
    using DataType = classtype;                                                \
    static_assert(std::is_base_of_v<PBCommon, classtype>,                      \
                  "Data type must inherit from PBCommon");                     \
  };
AVOX_MAP_MP_PINGBACK(XX)
#undef XX

// 主模板
template <MPPBType T,
          typename DataType = typename MPPingbackDataMap<T>::DataType>
class XMPPingback : public MPPingback {
  static_assert(std::is_base_of_v<PBCommon, DataType>,
                "Data type must inherit from PBCommon");

public:
  explicit XMPPingback(const DataType &data_) : data(data_) {
    MPPingback::type = T;
  }
  virtual ~XMPPingback() {}
  const DataType &getData() const { return data; }
  DataType &getData() { return data; }
  void setData(const DataType &data_) { data = data_; }

protected:
  DataType data = {};

protected:
  virtual void onToJson(Json &json) override { data.toJson(json); }
  virtual bool onFromJson(Json &json) override { return data.fromJson(json); }

public:
  virtual void logPB() override{
    std::ostringstream oss;
    // oss << getMPPBTypeStr(type) << " ";
    oss << (int32_t)type << "*. ";
    data.logPB(oss);
    log(data.getLevel(), oss.str().c_str());
  }
};

// 埋点队列与线程
class MPPingQueue : public RunTask {
public:
  MPPingQueue();
  virtual ~MPPingQueue();

protected:
  // 是否记录播放PTS
  bool bLogPts = false;
  // 是否记录实时码率
  bool bLogBitrate = false;

  RingBuffer<MPPingbackPtr> mpPingbacks;
  IPingbackOb *pingbackOb = nullptr;
  static int32_t mpid;
  // 队列满丢弃的累计条数: 满了不反压帧路径, 但要可观测(否则"埋点变少"分不清是采样还是丢包)
  std::atomic<uint64_t> droppedCount{0};

public:
  bool canLogPts() { return bLogPts; }
  bool canLogBitrate() { return bLogBitrate; }
  void setPingbackOb(IPingbackOb *ob) { pingbackOb = ob; }
  // 累计丢弃条数, 供宿主与日志观测
  uint64_t dropped() const {
    return droppedCount.load(std::memory_order_relaxed);
  }
  // 取出队列中的埋点并输出
  void depueuePB();

  template <MPPBType T>
  void pushPB(const typename MPPingbackDataMap<T>::DataType &data) {
    auto pingback = std::make_shared<XMPPingback<T>>(data);
    pingback->timespan = {localTimeStampMS() * 10000};
    pingback->mpid = mpid;
    // 队列满直接丢弃: 埋点是统计性数据不可反压帧路径 — 满时若在调用线程
    // 同步 depueuePB, 宿主回调链(shim→Dart)的慢会拖住渲染/解码线程
    // (panvox 每秒顿实证 2026-10-01, 禁回调后 PTS 步进 33-50 宽散→39-43 紧凑)
    // enqueue 满时不写入并返回 false(见 RingBuffer::enqueue), 排空交给 onRunTask 线程
    if (!mpPingbacks.enqueue(pingback)) {
      droppedCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
  // 输出到IO，如果有可能，专门在一个线程上处理
  virtual void onRunTask() override;
  // 收尾前排空残余: 停止后再无消费者, 留在队列里的埋点会静默丢失
  virtual void onStopTask() override;
};

template <MPPBType T>
void pushPB(MPPingQueue *obj,
            const typename MPPingbackDataMap<T>::DataType &data) {
  if (obj) {
    obj->pushPB<T>(data);
  }
}

MPPingbackPtr pbFromJson(const char *jsonstr);

}
