#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"

// libsmb2 预编译头(库仓, 见 cmake/FindLibsmb2.cmake)
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

namespace avox {

// IRemoteSource 实现(avox_remote): SMB 会话(libsmb2, 同步API跑在RunTask线程)。
// open=smb2_connect_share 验会话, list=smb2_opendir+readdir 枚举目录,
// resolve=拼规范 smb://host[:port]/share/path 交给 IOParseSmb(libsmb2 自定义avio)播放。
// token 约定: share 内绝对路径, '/'开头, 目录带尾'/', 根为"/"(open 带 /sub 路径时
// 顶层"/"映射到该子目录); 会话无跨目录状态, 每次 list 独立连接。
// user 支持 "DOMAIN\user"(libsmb2 原生拆分); URL 可带 userinfo(参数优先)。
// 线程模型同 DavSource: 单发 RunTask 工作线程跑 op, 结果锁保护读取。
class SmbSource : public IRemoteSource, public RunTask {
 public:
  SmbSource();
  virtual ~SmbSource();

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
  // 结果批次条目(list 输出: 目录或文件)
  struct Entry {
    RemoteEntryType type = RemoteEntryType::other;
    std::string name = "";
    std::string token = "";
    uint64_t size = 0;
  };
  // 入口 URL 解析产物(resolve/会话复用)
  struct UrlParts {
    std::string user = "";
    std::string pass = "";
    std::string host = "";
    int port = 445;
    std::string share = "";  // 共享名(URL 第一段路径, 必填)
    std::string rootToken = "/";  // share 内初始路径(目录带尾'/')
  };
  // 工作线程本次要跑的操作
  enum class Op { open, list };

  // 工作线程分派: 验会话 / 列目录
  void runOpen();
  void runList();
  // smb2dirent → 批次条目(跳过 . ..; token = dirToken + name, 绝对路径)
  bool fillBatch(struct smb2_context* ctx, struct smb2dir* dir,
                 const std::string& dirToken);

  // token 工具(与 DavSource 同口径)
  static std::string baseName(const std::string& token);
  static bool isDirToken(const std::string& token);
  // 解析入口 URL: user/pass/host/port/share/rootToken (非法返回 false)
  static bool parseUrl(const std::string& url, UrlParts* out);
  // token → 规范播放 URL: smb://[user:pass@]host[:port]/share/path
  std::string entryUrl(const std::string& token) const;

 private:
  // 观察者(单播, 工作线程回调)
  std::atomic<IRemoteSourceOb*> obAt{nullptr};
  // 请求参数(op 启动快照)
  Op op = Op::open;
  std::string reqUrl;
  std::string listToken;
  int32_t opTimeoutMs = 10000;
  // 入口解析产物(open 成功后不变, 读多写少, resultMutex 保护)
  std::unique_ptr<UrlParts> parts;
  // 外部中止源(close/stopList 置位; 阻塞调用返回后检查丢弃结果)
  std::atomic<bool> abortFlag{false};
  // 会话状态
  std::atomic<bool> openedFlag{false};
  // 结果(结果锁: 工作线程写, getter 读)
  std::mutex resultMutex;
  std::vector<Entry> batch;
  std::string sessionName;
  std::string lastError;
  std::string resolvedBuf;  // resolve 返回缓冲(会话内稳定)
};

}
