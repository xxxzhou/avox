#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"
#include "RemoteBridge.hpp"

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
  // 直链失效重取 (a05-T3 契约 §2): 对文件条目重发 PROPFIND Depth0, 以服务端
  // 重签的 href 换新直链(alist/网盘形态 URL 有时效; 静态 DAV 返回同路径)。
  // 同步阻塞(至多 opTimeoutMs), 失败 nullptr(原因见 getLastError)。
  virtual const char* refresh(int32_t entryIndex, IOption* option) override;
  // 会话中途鉴权过期后的重授权 (a05 §1): 新凭据即刻生效并重新武装 onAuthExpired
  // (同一会话至多抛一次, 重授权成功后可再抛); entryIndex>=0 时同步重取该条目
  // 直链续播, 浏览场景下次 list 自然生效。false = 会话未建立或重取失败。
  virtual bool reauthorize(int32_t entryIndex, const char* user,
                           const char* pass, const char* token) override;

 public:
  // ---- 断链自愈桥 (davbridge, 仅 IOParseDav 调用) ----
  // 本会话最近 resolve/refresh 产出的直链是否为 url
  bool matchPlaybackUrl(const std::string& url);
  // 重取最近 resolve 条目的直链, 新 URL 写 *out (复用 refresh 通道)
  bool refreshPlaybackUrl(std::string* out);
  // 会话鉴权 Header 注入对 (setParam("authHeader") 产物; IOParseDav 直链 GET 携带)
  void playbackAuthHeader(std::string* key, std::string* val);
  // 会话是否处于鉴权过期态 (a05 §1; IOParseDav 等待重授权轮询用)
  bool authExpired();

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
  // 会话中途鉴权过期统一入口 (a05 §1): 置过期态 + lastError, 武装位允许时抛
  // onAuthExpired (同一会话至多一次, reauthorize 成功后重新武装)。不持锁回调。
  void handleAuthFailure(int32_t status);
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
  std::string entryUrl(const std::string& token, const std::string& user,
                       const std::string& pass) const;  // token → 播放直链(带userinfo)
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
  // 鉴权 Header 注入 (a05-T3 契约 §3, setParam("authHeader","Key:Value") 写入;
  // 本会话所有 PROPFIND 携带, refreshPlaybackUrl 时交 IOParseDav 携带到直链 GET)
  std::string authKey;
  std::string authVal;
  // 入口解析产物(open 成功后不变, 读多写少, resultMutex 保护)
  std::unique_ptr<UrlParts> parts;
  // 外部中止源(close/stopList 置位; httplib 请求不可中断, 请求返回后检查丢弃结果)
  std::atomic<bool> abortFlag{false};
  // 会话状态
  std::atomic<bool> openedFlag{false};
  // 鉴权三态 (a05 §1): expired = 会话中途 401/403 后未重授权 (浏览 list 仍可发,
  // 回调面 code=authExpired); armed = onAuthExpired 武装位, 至多抛一次
  std::atomic<bool> authExpiredFlag{false};
  std::atomic<bool> authCbArmed{true};
  // 生效 Basic 凭据快照 (resultMutex 保护; reauthorize 热更新, propfind/entryUrl 读)
  std::string credUser;
  std::string credPass;
  // onAuthExpired 的 sourceId (创建时的协议键, RemoteModule.reg 同名)
  const std::string sourceId = "dav";
  // 结果(结果锁: 工作线程写, getter 读)
  std::mutex resultMutex;
  std::vector<Entry> batch;
  std::string sessionName;
  std::string lastError;
  std::string resolvedBuf;  // resolve 返回缓冲(会话内稳定)
  // 最近一次 resolve/refresh 的条目与产出 (断链自愈桥的匹配凭证)
  int32_t lastResolvedIndex = -1;
  std::string lastResolvedUrl;
};

namespace davbridge {
// 按播放直链找注册会话(resolve/refresh 产出原样匹配; 未命中 nullptr)。
// 需 DavSource 完整类型, 置类定义后。
inline DavSource* findForUrl(const std::string& url) {
  std::lock_guard<std::mutex> lk(regMutex());
  auto& v = reg();
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i]->matchPlaybackUrl(url)) {
      return v[i];
    }
  }
  return nullptr;
}
}  // namespace davbridge

}
