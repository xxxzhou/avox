#pragma once

// 断链自愈桥 (a05-T3): IOParseDav 与 DavSource 同插件内的会话注册表。
// 播放中直链失效(401/403/404/410)时, IOParseDav 用「resolve 产出本直链的
// DavSource 会话」重取 refresh 换新直链续播; 会话随 DavSource 构造/析构
// 注册/注销。仅插件内部使用, 不进公开头, 也不动引擎核心。
//
// 匹配口径: IOParseDav 手里的原始播放 URL 与 DavSource 最近一次 resolve/
// refresh 的产出原样相等(同一会话产的直链字符串不变式)。
// 线程模型: 注册表互斥; 命中后的 refresh 调用方(IOParseDav IO 线程)与
// DavSource 内部结果锁串行, 不经本锁。

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace avox {

class DavSource;

namespace davbridge {

// 代理地址解析(隧道网关用): 值形如 "http://127.0.0.1:17817", 剥 scheme/
// userinfo/路径取 host+port。两处 cpp-httplib 客户端(IOParseDav 播放 /
// DavSource 列表)共用; 端口缺失或非法返回 false(调用方忽略代理并告警)。
inline bool parseProxyUrl(const std::string& url, std::string* host,
                          int32_t* port) {
  std::string s = url;
  const size_t scheme = s.find("://");
  if (scheme != std::string::npos) {
    s = s.substr(scheme + 3);
  }
  const size_t slash = s.find('/');
  if (slash != std::string::npos) {
    s = s.substr(0, slash);
  }
  const size_t at = s.rfind('@');  // 用户信息段丢弃(隧道网关无认证)
  if (at != std::string::npos) {
    s = s.substr(at + 1);
  }
  std::string h;
  int32_t p = -1;
  if (!s.empty() && s[0] == '[') {  // IPv6 字面量 [::1]:port
    const size_t rb = s.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    h = s.substr(1, rb - 1);
    if (rb + 1 < s.size() && s[rb + 1] == ':') {
      p = std::atoi(s.c_str() + rb + 2);
    }
  } else {
    const size_t colon = s.rfind(':');
    if (colon != std::string::npos && s.find(':') == colon) {
      h = s.substr(0, colon);
      p = std::atoi(s.c_str() + colon + 1);
    } else {
      h = s;
    }
  }
  if (h.empty() || p <= 0 || p > 65535) {
    return false;
  }
  *host = h;
  *port = p;
  return true;
}

inline std::mutex& regMutex() {
  static std::mutex m;
  return m;
}
inline std::vector<DavSource*>& reg() {
  static std::vector<DavSource*> v;
  return v;
}

inline void registerSource(DavSource* s) {
  std::lock_guard<std::mutex> lk(regMutex());
  reg().push_back(s);
}
inline void unregisterSource(DavSource* s) {
  std::lock_guard<std::mutex> lk(regMutex());
  auto& v = reg();
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == s) {
      v[i] = v.back();
      v.pop_back();
      break;
    }
  }
}

}  // namespace davbridge

}  // namespace avox
