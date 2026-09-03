#include "MPCommon.hpp"

#include "../module/LogHelper.hpp"

namespace avox {

#define AVOX_INT64_VAILD(value) (value == INT64_MIN ? -1 : value)

const char *getMPPBTypeStr(MPPBType type) {
  switch (type) {
#define XX(name, value, str, classtype, tick)                                  \
  case MPPBType::name:                                                         \
    return str;
    AVOX_MAP_MP_PINGBACK(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *getQueuTypeStr(PBQueueType type) {
  switch (type) {
#define XX(name, value, str)                                                   \
  case PBQueueType::name:                                                      \
    return str;
    AVOX_MAP_PB_QUEUE_TYPE(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *getDirectionStr(PBDirectionType type) {
  switch (type) {
#define XX(name, value, str)                                                   \
  case PBDirectionType::name:                                                  \
    return str;
    AVOX_MAP_DIRECTION_TYPE(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *getMediaObjectStr(MediaObject type) {
  switch (type) {
#define XX(name, value, str)                                                   \
  case MediaObject::name:                                                      \
    return str;
    AVOX_MAP_MEDIA_OBJECT_TYPE(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *getMediaActionStr(MediaAction type) {
  switch (type) {
#define XX(name, value, str)                                                   \
  case MediaAction::name:                                                      \
    return str;
    AVOX_MAP_MEDIA_OBJECT_ACTION_TYPE(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *getActionResultStr(ActionResult type) {
  switch (type) {
#define XX(name, value, str)                                                   \
  case ActionResult::name:                                                     \
    return str;
    AVOX_MAP_ACTION_RESULT_TYPE(XX)
#undef XX
  default:
    return "unknow";
  }
}

const char *jsonGetStr(Json &jdata, const char *str) {
  return jdata[str].bString() ? jdata.getString(str) : "";
}

int jsonGetInt(Json &jdata, const char *str) {
  return jdata[str].bInt() ? jdata.getInt(str) : 0;
}

double jsonGetDouble(Json &jdata, const char *str) {
  return jdata[str].bNumber() ? jdata.getDouble(str) : 0.0;
}

bool jsonGetBool(Json &jdata, const char *str) {
  return jdata[str].bBool() ? jdata.getBool(str) : false;
}

void PBMediaAction::toJson(Json &jdata) {
  jdata["mediaObject"] = (int64_t)mediaObject;
  jdata["trackType"] = (int64_t)trackType;
  jdata["action"] = (int64_t)action;
  jdata["result"] = (int64_t)result;
  jdata["msg"] = msg;
}

bool PBMediaAction::fromJson(Json &jdata) {
  mediaObject = static_cast<MediaObject>(jsonGetInt(jdata, "mediaObject"));
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  action = static_cast<MediaAction>(jsonGetInt(jdata, "action"));
  result = static_cast<ActionResult>(jsonGetInt(jdata, "result"));
  msg = jsonGetStr(jdata, "msg");
  return true;
}

void PBMediaAction::logPB(std::ostream &o) {
  if (trackType != TrackType::none) {
    string_format(o, getTrackTypeStr(trackType), " ");
  }
  string_format(o, getMediaObjectStr(mediaObject));
  if (action != MediaAction::none || action != MediaAction::message) {
    string_format(o, " ", getMediaActionStr(action));
  }
  if (result != ActionResult::none) {
    string_format(o, " result: ", getActionResultStr(result));
  }
  if (!msg.empty()) {
    string_format(o, " msg: ", msg);
  }
}

LogLevel PBMediaAction::getLevel() {
  if (result == ActionResult::fail) {
    return LogLevel::warn;
  }
  return LogLevel::info;
}

void PBIOStatus::toJson(Json &jdata) {
  jdata["url"] = url;
  jdata["baseTimeMs"] = baseTimeMs;
  jdata["baseVideoMs"] = baseVideoMs;
  jdata["baseAudioMs"] = baseAudioMs;
  jdata["videoCount"] = videoCount;
  jdata["audioCount"] = audioCount;
  jdata["subtitleCount"] = subtitleCount;
}

bool PBIOStatus::fromJson(Json &jdata) {
  url = jsonGetStr(jdata, "url");
  baseTimeMs = jsonGetInt(jdata, "baseTimeMs");
  baseVideoMs = jsonGetInt(jdata, "baseVideoMs");
  baseAudioMs = jsonGetInt(jdata, "baseAudioMs");
  videoCount = jsonGetInt(jdata, "videoCount");
  audioCount = jsonGetInt(jdata, "audioCount");
  subtitleCount = jsonGetInt(jdata, "subtitleCount");
  return true;
}

void PBIOStatus::logPB(std::ostream &o) {
  string_format(o, "url:", url, " videoCount:", videoCount,
                " audioCount:", audioCount, " subtitleCount:", subtitleCount,
                " baseTimeMs:", baseTimeMs);
  if (videoCount > 0) {
    string_format(o, " baseVideoMs:", baseVideoMs);
  }
  if (audioCount > 0) {
    string_format(o, " baseAudioMs:", baseAudioMs);
  }
}

void PBVideoInfo::toJson(Json &jdata) {
  jdata["slectCodec"] = slectCodec;
  jdata["codecId"] = (int64_t)codecId;
  jdata["yuvType"] = (int64_t)yuvType;
  jdata["renderType"] = (int64_t)renderType;
  jdata["width"] = width;
  jdata["height"] = height;
  jdata["fps"] = fps;
}
// PBVideoInfo 修改
bool PBVideoInfo::fromJson(Json &jdata) {
  slectCodec = jsonGetStr(jdata, "slectCodec");
  codecId = static_cast<VCodecId>(jsonGetInt(jdata, "codecId"));
  yuvType = static_cast<YuvType>(jsonGetInt(jdata, "yuvType"));
  renderType = static_cast<RenderType>(jsonGetInt(jdata, "renderType"));
  width = jsonGetInt(jdata, "width");
  height = jsonGetInt(jdata, "height");
  fps = jsonGetDouble(jdata, "fps");
  return true;
}

void PBVideoInfo::logPB(std::ostream &o) {
  string_format(o, "codecId:", getVCodecName(codecId),
                " slectCodec:", slectCodec,
                " renderType:", getVRenderTypeStr(renderType),
                " yuvType:", getYuvTypeStr(yuvType), " width:", width,
                " height:", height, " fps:", fps);
}

void PBAudioInfo::toJson(Json &jdata) {
  jdata["slectCodec"] = slectCodec;
  jdata["codecId"] = (int64_t)codecId;
  jdata["renderType"] = (int64_t)renderType;
  jdata["sampleRate"] = sampleRate;
  jdata["channels"] = channels;
  jdata["format"] = (int64_t)format;
  jdata["xsampleRate"] = xsampleRate;
  jdata["xchannels"] = xchannels;
  jdata["xformat"] = (int64_t)xformat;
}

bool PBAudioInfo::fromJson(Json &jdata) {
  slectCodec = jsonGetStr(jdata, "slectCodec");
  codecId = static_cast<ACodecId>(jsonGetInt(jdata, "codecId"));
  renderType = static_cast<ARenderType>(jsonGetInt(jdata, "renderType"));
  sampleRate = jsonGetInt(jdata, "sampleRate");
  channels = jsonGetInt(jdata, "channels");
  format = static_cast<AudioFormat>(jsonGetInt(jdata, "format"));
  // 新增扩展字段处理
  xchannels = jsonGetInt(jdata, "xchannels");
  xsampleRate = jsonGetInt(jdata, "xsampleRate");
  xformat = static_cast<AudioFormat>(jsonGetInt(jdata, "xformat"));
  return true;
}

void PBAudioInfo::logPB(std::ostream &o) {
  string_format(
      o, "codecId:", getACodecName(codecId), " slectCodec:", slectCodec,
      " renderType:", getARenderTypeStr(renderType), " sampleRate:", sampleRate,
      " channels:", channels, " format:", getAudioFormatStr(format),
      " out sampleRate:", xsampleRate, " out channels:", xchannels,
      " out format:", getAudioFormatStr(xformat));
}

void PBMediaIssue::toJson(Json &jdata) {
  jdata["errorType"] = (int64_t)errorType;
  jdata["trackType"] = (int64_t)trackType;
  jdata["code"] = code;
  jdata["msg"] = msg;
}

// PBError 修改
bool PBMediaIssue::fromJson(Json &jdata) {
  errorType = static_cast<MediaObject>(jsonGetInt(jdata, "errorType"));
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  code = jsonGetInt(jdata, "code");
  msg = jsonGetStr(jdata, "msg");
  return true;
}

void PBMediaIssue::logPB(std::ostream &o) {
  if (trackType != TrackType::none) {
    string_format(o, getTrackTypeStr(trackType));
  }
  string_format(o, getMediaObjectStr(errorType), " error,code:", code,
                " msg:", msg);
}

LogLevel PBMediaIssue::getLevel() { return LogLevel::error; }

void PBStateChange::toJson(Json &jdata) {
  jdata["preState"] = (int64_t)preState;
  jdata["state"] = (int64_t)state;
}
// PBStateChange 修改
bool PBStateChange::fromJson(Json &jdata) {
  preState = static_cast<PlayerState>(jsonGetInt(jdata, "preState"));
  state = static_cast<PlayerState>(jsonGetInt(jdata, "state"));
  return true;
}

void PBStateChange::logPB(std::ostream &o) {
  string_format(o, "media player state from ", getPlayerStateStr(preState),
                " to ", getPlayerStateStr(state));
}

void PBQueueSize::toJson(Json &jdata) {
  jdata["trackType"] = (int64_t)trackType;
  jdata["queueType"] = (int64_t)queueType;
  jdata["directionType"] = (int64_t)directionType;
  jdata["queueSize"] = queueSize;
  jdata["dataPts"] = dataPts;
  jdata["dataSize"] = dataSize;
  jdata["frameType"] = frameType;
}

// 其他类的修改模式相同，以 PBQueueSize 为例：
bool PBQueueSize::fromJson(Json &jdata) {
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  queueType = static_cast<PBQueueType>(jsonGetInt(jdata, "queueType"));
  directionType =
      static_cast<PBDirectionType>(jsonGetInt(jdata, "directionType"));
  queueSize = jsonGetInt(jdata, "queueSize");
  dataPts = jsonGetInt(jdata, "dataPts");
  dataSize = jsonGetInt(jdata, "dataSize");
  frameType = jsonGetInt(jdata, "frameType");
  return true;
}

void PBQueueSize::logPB(std::ostream &o) {
  string_format(o, getTrackTypeStr(trackType), " ", getQueuTypeStr(queueType),
                " ", getDirectionStr(directionType), " size:", queueSize,
                " pts:", dataPts);
}

void PBQueueStatus::toJson(Json &jdata) {
  jdata["trackType"] = (int64_t)trackType;
  jdata["renderTime"] = renderTime;
  jdata["decodeOutTime"] = decodeOutTime;
  jdata["decodeInTime"] = decodeInTime;
  jdata["ioTime"] = ioTime;
}

bool PBQueueStatus::fromJson(Json &jdata) {
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  renderTime = jsonGetInt(jdata, "renderTime");
  decodeOutTime = jsonGetInt(jdata, "decodeOutTime");
  decodeInTime = jsonGetInt(jdata, "decodeInTime");
  ioTime = jsonGetInt(jdata, "ioTime");
  return true;
}

void PBQueueStatus::logPB(std::ostream &o) {
  string_format(o, getTrackTypeStr(trackType), " frame queue pts(",
                AVOX_INT64_VAILD(renderTime), "-",
                AVOX_INT64_VAILD(decodeOutTime), "),io queue time(",
                AVOX_INT64_VAILD(decodeInTime), "-", AVOX_INT64_VAILD(ioTime),
                ")", " frame size:", frameSize, " queue size:", queueSize);
}

void PBPtsTick::toJson(Json &jdata) {
  jdata["trackType"] = (int64_t)trackType;
  jdata["pts"] = pts;
}

bool PBPtsTick::fromJson(Json &jdata) {
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  pts = jsonGetInt(jdata, "pts");
  return true;
}

void PBPtsTick::logPB(std::ostream &o) {
  string_format(o, getTrackTypeStr(trackType), " play pts:", pts);
}

void PBRateRecord::toJson(Json &jdata) {
  jdata["trackType"] = (int64_t)trackType;
  jdata["rate"] = rate;
}

// PBRateRecord 修改
bool PBRateRecord::fromJson(Json &jdata) {
  trackType = static_cast<TrackType>(jsonGetInt(jdata, "trackType"));
  rate = jsonGetDouble(jdata, "rate");
  return true;
}

void PBRateRecord::logPB(std::ostream &o) {
  string_format(o, getTrackTypeStr(trackType), " rate:", rate);
}

void PBPlaySpeed::toJson(Json &jdata) { jdata["speed"] = speed; }

bool PBPlaySpeed::fromJson(Json &jdata) {
  speed = jsonGetDouble(jdata, "speed");
  return true;
}

void PBPlaySpeed::logPB(std::ostream &o) {
  string_format(o, "media player speed:", speed);
}

void PBWindowStatus::getWindowInfo(Window *win) {
  if (!win) {
    return;
  }
  width = win->getWidth();
  height = win->getHeight();
  renderType = win->getRenderType();
}

void PBWindowStatus::toJson(Json &jdata) {
  jdata["width"] = width;
  jdata["height"] = height;
  jdata["renderType"] = (int64_t)renderType;
}

bool PBWindowStatus::fromJson(Json &jdata) {
  width = jsonGetInt(jdata, "width");
  height = jsonGetInt(jdata, "height");
  renderType = static_cast<RenderType>(jsonGetInt(jdata, "renderType"));
  return true;
}

void PBWindowStatus::logPB(std::ostream &o) {
  string_format(o, "window size:", width, "x", height,
                " renderType:", getVRenderTypeStr(renderType));
}

}
