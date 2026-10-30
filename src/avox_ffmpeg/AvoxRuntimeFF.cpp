// AvoxRuntime 的 FFmpeg 实现: 版本/编码器/muxer/协议能力查询。
// isVulkanAvailable 在 src/avox/module/AvoxRuntime.cpp(核心侧, 不依赖 FFmpeg)

#include "avox/AvoxRuntime.h"

#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace avox {

FFmpegVersionInfo getFFmpegVersionInfo() {
  FFmpegVersionInfo info;
  info.avcodecVersion = avcodec_version();
  info.avformatVersion = avformat_version();
  info.avutilVersion = avutil_version();
  return info;
}

const char* getFFmpegVersion() {
  return av_version_info();
}

const char* getFFmpegBuildConfig() {
  return avcodec_configuration();
}

bool isEncoderRegistered(const char* name) {
  return name && *name && avcodec_find_encoder_by_name(name) != nullptr;
}

bool hasMuxer(const char* name) {
  return name && *name && av_guess_format(name, nullptr, nullptr) != nullptr;
}

namespace {

// 协议名只在首次访问时枚举一次缓存(avio_enum_protocols 遍历内部链表),
// 之后只读, c_str() 稳定
struct ProtocolCache {
  std::mutex mtx;
  std::vector<std::string> in;
  std::vector<std::string> out;
  bool inDone = false;
  bool outDone = false;

  std::vector<std::string>& list(bool output) { return output ? out : in; }
  bool& done(bool output) { return output ? outDone : inDone; }

  void ensure(bool output) {
    if (done(output)) {
      return;
    }
    void* opaque = nullptr;
    // avio_enum_protocols 第二参: 0=input, 1=output
    while (const char* name = avio_enum_protocols(&opaque, output ? 1 : 0)) {
      if (name) {
        list(output).push_back(name);
      }
    }
    done(output) = true;
  }
};

ProtocolCache& protocolCache() {
  static ProtocolCache cache;
  return cache;
}

}  // namespace

int32_t getProtocolCount(bool output) {
  ProtocolCache& cache = protocolCache();
  std::lock_guard<std::mutex> lock(cache.mtx);
  cache.ensure(output);
  return (int32_t)cache.list(output).size();
}

const char* getProtocolName(bool output, int32_t index) {
  ProtocolCache& cache = protocolCache();
  std::lock_guard<std::mutex> lock(cache.mtx);
  cache.ensure(output);
  auto& list = cache.list(output);
  if (index < 0 || index >= (int32_t)list.size()) {
    return "";
  }
  return list[(size_t)index].c_str();
}

}  // namespace avox
