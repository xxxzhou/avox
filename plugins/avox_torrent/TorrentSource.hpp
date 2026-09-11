#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"
#include "TorrentEngine.hpp"

namespace avox {

// IRemoteSource 实现(avox_torrent): 磁力/BT 会话。open=探元数据(慢, 可超时中止),
// list=元数据路径树即时枚举(目录 token 带尾部'/'下钻), resolve=写 torrent.fileIndex
// 返回原链。线程模型: 单发 RunTask 工作线程跑 op, 结果拷成纯值, 结果锁保护读取。
class TorrentSource : public IRemoteSource, public RunTask {
 public:
  TorrentSource();
  // 析构定义在cpp: engine 持有前向声明类型的 unique_ptr, 需完整类型可见处销毁
  virtual ~TorrentSource();

 public:
  virtual void setOb(IRemoteSourceOb* ob) override;
  virtual uint32_t getCaps() override;
  virtual RemoteAuthKind getAuthKind() override;
  virtual void setParam(const char* key, const char* value) override;

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
  // 工作线程分派: 探测元数据 / 枚举路径树
  void runOpen();
  void runList();
  // 探测结果 → 路径树 (resultMutex 内调用; 目录 token 带尾部'/', 根为空串)
  void buildTree();
  // "a/b/" -> "a/", "a/" -> ""
  static std::string parentToken(const std::string& dirToken);

 private:
  // 探测结果条目(种子内原始 index + 完整路径/大小/媒体标注)
  struct FileEntry {
    int32_t index = -1;
    std::string path = "";
    uint64_t size = 0;
    bool media = false;
  };
  // 结果批次条目(list 输出: 目录或文件)
  struct Entry {
    RemoteEntryType type = RemoteEntryType::other;
    std::string name = "";
    std::string token = "";
    uint64_t size = 0;
    int32_t fileIndex = -1;  // media 条目对应的种子内文件 index, 其余 -1
  };
  // 工作线程本次要跑的操作
  enum class Op { open, list };

  // 观察者(单播, 工作线程回调)
  std::atomic<IRemoteSourceOb*> obAt{nullptr};
  // 请求参数(op 启动快照)
  Op op = Op::open;
  std::string reqUrl;
  std::string listToken;
  TorrentEngine::Config cfg = {};
  // 会话参数(setParam 写入, open 时快照进 cfg)
  std::string paramCacheDir;
  std::string paramExtraTrackers;
  // 外部中止源(close/stopList 置位 -> engine 等元数据 50ms 内退出)
  std::atomic<bool> abortFlag{false};
  // 会话状态
  std::atomic<bool> openedFlag{false};
  // 探测结果(结果锁: 工作线程写, getter 读)
  std::mutex resultMutex;
  std::vector<FileEntry> files;
  // 路径树: 目录 token -> 子条目(目录在前, 名称升序; 根 token 为空串)
  std::map<std::string, std::vector<Entry>> tree;
  std::vector<Entry> batch;  // 最近一次 list 的结果批次
  std::string torrentName;
  std::string infoHash;
  uint64_t totalSize = 0;
  std::string totalSizeBuf;  // getSessionField("totalSize") 的返回缓冲
  std::string lastError;
  std::string resolvedBuf;   // resolve 返回缓冲(会话内稳定)
};

}
