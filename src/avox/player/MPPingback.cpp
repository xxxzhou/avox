#include "MPPingback.hpp"

namespace avox {

int32_t MPPingQueue::mpid = 0;

MPPingQueue::MPPingQueue() {
  mpPingbacks.setMaxSize(200);
  taskName = "media play pingback task";
  bLogPts = false;
  bLogBitrate = false;
  mpid++;
}

MPPingQueue::~MPPingQueue(){
  stopTask();
}

void MPPingQueue::depueuePB() {
  MPPingbackPtr pingback = nullptr;
  while (!mpPingbacks.empty()) {
    if (mpPingbacks.dequeue(pingback)) {
      if (pingback) {
        pingback->logPB();
        if (pingbackOb) {
          pingbackOb->onPingback((int32_t)pingback->type,
                                 pingback->toJson().c_str());
        }
      }
    }
  }
}

void MPPingQueue::onRunTask() {
  while (running()) {
    depueuePB();
    sleepTask(false, 10);
  }
}

MPPingbackPtr pbFromJson(const char* jsonstr) {
  Json json = parserJson(jsonstr);
  if (!json["type"].bInt() || !json["timespan"].bInt()) {
    return nullptr;
  }
  MPPBType pbType = (MPPBType)(json.getInt("type"));
  switch (pbType) {
#define XX(name, value, str, classtype, tick)                            \
  case MPPBType::name: {                                                 \
    using DataType = classtype;                                          \
    DataType data = {};                                                  \
    if (!(data.fromJson(json["data"]))) {                                \
      return nullptr;                                                    \
    }                                                                    \
    auto pingback = std::make_shared<XMPPingback<MPPBType::name>>(data); \
    pingback->timespan = {json["timespan"]};                             \
    pingback->mpid = json.getInt("mpid");                                \
    return pingback;                                                     \
  }
    AVOX_MAP_MP_PINGBACK(XX)
#undef XX
    default:
      return nullptr;
  }
}

}
