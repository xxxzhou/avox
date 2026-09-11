#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"

namespace avox {

// IRemoteSource 实现(avox_remote): WebDAV 会话 (兼容 Alist/OpenList 的 /dav 端点)。
// open=PROPFIND Depth0 验会话, list=PROPFIND Depth1 列目录 (Basic 认证),
// resolve=token 拼 http(s) 直链(带 userinfo)交给现有 http IO 播放。
// token 约定: 解码后的服务器绝对路径, 目录带尾'/', 根为入口路径(补尾'/')。
// 线程模型同 TorrentSource: 单发 RunTask 工作线程跑 op, 结果锁保护读取。
class DavSource : public IRemoteSource, public RunTask {
 public:
  DavSource();
  virtual ~DavSource();

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
  // 入口 URL 解析产物(scheme/user/pass/host/port 全程不变, list/resolve 复用)
  struct UrlParts {
    std::string scheme = "http";
    std::string user = "";
    std::string pass = "";
    std::string host = "";
    int port = 0;
    std::string rootToken = "/";  // 入口路径(解码, 补尾'/')
  };
  // PROPFIND 单个 response 块的原始字段(href 为编码原样)
  struct DavItem {
    std::string href = "";
    std::string displayName = "";
    uint64_t size = 0;
    bool dir = false;
  };
  // 工作线程本次要跑的操作
  enum class Op { open, list };

  // 工作线程分派: 验会话 / 列目录
  void runOpen();
  void runList();
  // PROPFIND 请求: depth 0 验在/1 列子项; 成功时 body 为 multistatus 原文
  bool propfind(const std::string& reqPath, int depth, std::string* body,
                int32_t* statusCode);
  // multistatus XML → 条目序列 (容错解析: 忽略 ns 前缀/大小写, 自闭合/无前缀都认)
  static std::vector<DavItem> parseMultistatus(const std::string& xml);
  // DavItem → 批次条目 (排除自身, 解码 href, 目录补尾'/')
  bool fillBatch(const std::vector<DavItem>& items, const std::string& reqDir);

  // token/路径/URL 工具
  static std::string dirOf(const std::string& filePath);    // 文件路径 → 所在目录 token
  static std::string baseName(const std::string& token);    // 末段名(去尾'/')
  static std::string parentToken(const std::string& dirToken);
  static bool isDirToken(const std::string& token);
  // 解析入口 URL: scheme/user/pass/host/port/rootToken (非法返回 false)
  static bool parseUrl(const std::string& url, UrlParts* out);
  std::string entryUrl(const std::string& token) const;     // token → 播放直链(带userinfo)
  std::string requestPath(const std::string& token) const;  // token → 逐段百分号编码

 private:
  // 观察者(单播, 工作线程回调)
  std::atomic<IRemoteSourceOb*> obAt{nullptr};
  // 请求参数(op 启动快照)
  Op op = Op::open;
  std::string reqUrl;
  std::string listToken;
  int32_t opTimeoutMs = 10000;
  // 会话参数(setParam 写入)
  bool verifyTls = true;
  // 入口解析产物(open 成功后不变, 读多写少, resultMutex 保护)
  std::unique_ptr<UrlParts> parts;
  // 外部中止源(close/stopList 置位; httplib 请求不可中断, 请求返回后检查丢弃结果)
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
