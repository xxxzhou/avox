#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"

namespace avox {

// IRemoteSource 实现(avox_disc): 蓝光原盘(ISO 镜像/BDMV 目录)。
// open=bd_open+标题枚举(本地元数据读, 快), list=标题+时长平列表(时长降序,
// 最长在前标正片; 绝不列 BDMV/STREAM 文件树——分节 m2ts 不可读), resolve=
// 写 disc.title 返回原链。DVD(VIDEO_TS)/AACS·BD+ 加密碟在 open 阶段明确报
// 不支持。线程模型同 TorrentSource: 单发 RunTask, 结果纯值+结果锁。
class DiscSource : public IRemoteSource, public RunTask {
 public:
  DiscSource();
  // 析构定义在cpp(同 TorrentSource 惯例)
  virtual ~DiscSource();

 public:
  virtual void setOb(IRemoteSourceOb* ob) override;
  virtual uint32_t getCaps() override;
  virtual RemoteAuthKind getAuthKind() override;

  virtual bool open(const char* url, const char* user, const char* pass,
                    const char* token, int32_t timeoutMs) override;
  virtual void close() override;
  virtual bool opened() override;

  virtual bool list(const char* nodeToken, int32_t timeoutMs) override;
  virtual void stopList() override;

  virtual int32_t getEntryCount() override;
  virtual RemoteEntryType getEntryType(int32_t i) override;
  virtual const char* getEntryName(int32_t i) override;
  virtual uint64_t getEntrySize(int32_t i) override;
  virtual const char* getEntryToken(int32_t i) override;
  virtual const char* getSessionField(const char* key) override;
  virtual const char* resolve(int32_t entryIndex, IOption* option) override;

  virtual const char* getLastError() override;

 protected:
  virtual void onRunTask() override;

 private:
  // 标题条目(bd 标题索引 + 展示名)
  struct TitleEntry {
    uint32_t bdIdx = 0;      // bd_get_titles 枚举序(bd_select_title 用)
    uint32_t playlist = 0;   // mpls id(日志/排查用)
    uint64_t durationTicks = 0;  // 90kHz
    std::string name = "";
    std::string token = "";
  };

 private:
  // 工作线程: open 探结构+枚举标题(list 即时, 无单独 op)
  void runOpen();

 private:
  std::atomic<IRemoteSourceOb*> obAt{nullptr};
  std::string reqUrl;
  std::atomic<bool> openedFlag{false};
  std::atomic<bool> abortFlag{false};
  std::mutex resultMutex;
  std::vector<TitleEntry> titles;
  std::string lastError;
  std::string resolvedBuf;   // resolve 返回缓冲(会话内稳定)
  std::string titleCountBuf; // getSessionField("titleCount") 返回缓冲
};

}
