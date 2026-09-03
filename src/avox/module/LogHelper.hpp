#pragma once

#include <assert.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "../Avox.hpp"

namespace avox {

// inline 版本: 零分配, 直接指向 __FILE__ 静态串内部, 跨 DLL 安全(每个 DLL
// 自己实例化)
inline const char* extractFileInline(const char* path) {
  if (!path) return "";
  const char* file = path;
  while (*path) {
    if (*path == '/' || *path == '\\') {
      file = path + 1;
    }
    path++;
  }
  return file;
}

// 写入文件:行号 方法
#define LOGFLF(level, ...)                                                   \
  log(level, extractFileInline(__FILE__), ":", __LINE__, " ", __func__, " ", \
      __VA_ARGS__)

#define LOGASSERT(ret, ...)                                  \
  if (!(ret)) {                                              \
    LOGFLF(LogLevel::error, "assert failed: ", __VA_ARGS__); \
  }                                                          \
  assert(ret);

#define LOG_RETURN(ret, result, ...)     \
  if (!(ret)) {                          \
    LOGFLF(LogLevel::warn, __VA_ARGS__); \
    return result;                       \
  }

#define TLOGFLF(logObj, level, ...)                                     \
  (logObj).olog(level, extractFileInline(__FILE__), ":", __LINE__, " ", \
                __func__, " ", __VA_ARGS__)

#define LOG_RETURN_FLASE(ret, ...) LOG_RETURN(ret, false, __VA_ARGS__)

// https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template <typename T>
void string_format(std::ostream& o, const T& t) {
  o << t;
}

template <typename T>
void string_format(std::ostream& o, const std::vector<T>& t) {
  o << "{";
  for (size_t i = 0; i < t.size(); i++) {
    if (i == t.size() - 1) {
      string_format(o, t[i]);
    } else {
      string_format(o, t[i]);
      o << ", ";
    }
  }
  o << "}";
}

template <typename T, typename... Args>
void string_format(std::ostream& o, const T& t, Args... args) {
  string_format(o, t);
  string_format(o, args...);
}

template <typename... Args>
void string_format(std::string& msg, Args... args) {
  std::ostringstream oss;
  string_format(oss, args...);
  msg = oss.str();
}

template <>
inline void string_format<>(std::ostream& o, const IP4Address& ip4) {
  o << (int32_t)ip4.arr1 << "." << (int32_t)ip4.arr2 << "." << (int32_t)ip4.arr3
    << "." << (int32_t)ip4.arr4;
}

template <>
inline void string_format<>(std::ostream& o, const IP4Endpoint& ip4) {
  string_format(o, ip4.address);
  o << ":" << ip4.port;
}

template <>
inline void string_format<>(std::ostream& o, const Timespan& timespan) {
  o << std::setfill('0') << std::setw(2) << timespan.getHours() << ":";
  // 格式化输出分钟部分，不足两位补0
  o << std::setfill('0') << std::setw(2) << timespan.getMinutes() << ":";
  // 格式化输出秒部分，不足两位补0
  o << std::setfill('0') << std::setw(2) << timespan.getSeconds() << ".";
  // 格式化输出毫秒部分，不足三位补0
  o << std::setfill('0') << std::setw(3) << timespan.getMilliSeconds();
}

template <>
inline void string_format<>(std::ostream& o, const AvoxData& data) {
  // 保存原始流格式状态
  std::ios::fmtflags old_flags = o.flags();
  o << "0x";
  for (int32_t i = 0; i < data.size; ++i) {
    o << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
      << (int)data.data[i];
  }
  o.flags(old_flags);
}

inline void string_format(std::ostream& o, YuvType yuvType) {
  o << getYuvTypeStr(yuvType);
}

inline void string_format(std::ostream& o, AudioFormat type) {
  o << getAudioFormatStr(type);
}

inline void string_format(std::ostream& o, ACodecId codec) {
  o << getACodecName(codec);
}

inline void string_format(std::ostream& o, VCodecId codec) {
  o << getVCodecName(codec);
}

inline void string_format(std::ostream& o, ImageType type) {
  o << getImageTypeStr(type);
}

inline void string_format(std::ostream& o, IoPlan type) {
  o << getIoPlanStr(type);
}

inline void string_format(std::ostream& o, MuxerType type) {
  o << getMuxerTypeStr(type);
}

template <>
inline void string_format<>(std::ostream& o, const AudioDesc& adesc) {
  string_format(o, adesc.format);
  o << "-" << adesc.sampleRate << "-" << adesc.channels;
}

template <>
inline void string_format<>(std::ostream& o, const ATrackDesc& adesc) {
  string_format(o, adesc.codecId);
  o << "-";
  string_format(o, adesc.desc);
}

template <>
inline void string_format<>(std::ostream& o, const VideoDesc& vdesc) {
  o << vdesc.width << "*" << vdesc.height << "@" << vdesc.fps << "-";
  string_format(o, vdesc.type);
}

template <>
inline void string_format<>(std::ostream& o, const VTrackDesc& vdesc) {
  o << getVCodecName(vdesc.codecId) << "-";
  string_format(o, vdesc.desc);
}

template <>
inline void string_format<>(std::ostream& o, const ImageFormat& format) {
  o << format.width << "*" << format.height << "-";
  string_format(o, format.imageType);
  o << "-[" << format.rowPitch << "]";
}

template <>
inline void string_format<>(std::ostream& o, const YUVFormat& format) {
  o << format.width << "*" << format.height << "-";
  string_format(o, format.type);
}

template <>
inline void string_format<>(std::ostream& o, const YUVFrame& frame) {
  o << "{pts:" << frame.pts << " dts:" << frame.dts << " stride:["
    << frame.stride[0] << "," << frame.stride[1] << "," << frame.stride[2]
    << "] ";
  string_format(o, frame.format);
  o << "}";
}

template <>
inline void string_format<>(std::ostream& o, const DecoderParams& params) {
  o << params.width << "*" << params.height << "@" << params.fps;
  string_format(o, params.yuvType);
}

template <typename... Args>
inline void log(LogLevel level, Args... args) {
  std::string msg = {};
  string_format(msg, args...);
  // 这里不直接使用log,可能会直接递归本函数,用logMsg
  logMsg(level, msg.c_str());
}

}
