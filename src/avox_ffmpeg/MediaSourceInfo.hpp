#pragma once

// reader 侧 ISourceInfo: 复用 BaseSource 轨道容器, 补容器级信息与原始 codec 名。
// 仅 open 期填充, onReady 后只读(换片=重建)

#include <deque>
#include <string>

#include "avox/source/BaseSource.hpp"

namespace avox {

class MediaSourceInfo : public BaseSource {
 public:
  MediaSourceInfo() = default;
  virtual ~MediaSourceInfo() = default;

 public:
  // ISourceInfo 1029 追加项
  virtual int64_t durationMs() override { return containerDurationMs; }
  virtual int64_t bitRate() override { return containerBitRate; }
  virtual const char* formatName() override {
    return containerFormatName.c_str();
  }
  // index=轨序号(与 getVideoDesc 同语义); 名单按轨道顺序存放
  virtual const char* getVideoCodecName(int32_t index) override;
  virtual const char* getAudioCodecName(int32_t index) override;
  // 容器 IO 可 seek(覆盖 BaseSource 的 bSeek 语义)
  virtual bool canSeek() override { return bSeekable; }

 public:
  // open 期填充(无并发)
  void setContainerInfo(int64_t durationMsValue, int64_t bitRateValue,
                        const std::string& formatNameValue, bool seekable);
  void addVideoCodecName(const std::string& name);
  void addAudioCodecName(const std::string& name);

 private:
  int64_t containerDurationMs = 0;
  int64_t containerBitRate = 0;
  std::string containerFormatName;
  bool bSeekable = false;
  // deque 追加不搬移元素, 已返回的 c_str() 不悬空
  std::deque<std::string> videoCodecNames;
  std::deque<std::string> audioCodecNames;
};

}  // namespace avox
