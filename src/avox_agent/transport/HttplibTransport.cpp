#include "HttplibTransport.hpp"

#define CPPHTTPLIB_NO_EXCEPTIONS
#include "httplib.h"

namespace avox {

// httplib 错误码 → 可读字符串 (从 AgentClientSSE.cpp 搬入; vaildHttps 侧保留原副本)
static const char* httplibErrorString(httplib::Error err) {
  switch (err) {
    case httplib::Error::Success:                       return "Success";
    case httplib::Error::Connection:                    return "Connection failed";
    case httplib::Error::ConnectionTimeout:             return "Connection timeout";
    case httplib::Error::ConnectionClosed:              return "Connection closed";
    case httplib::Error::Read:                          return "Read error";
    case httplib::Error::Write:                         return "Write error";
    case httplib::Error::Timeout:                       return "Read timeout";
    case httplib::Error::SSLConnection:                 return "SSL connection failed";
    case httplib::Error::SSLLoadingCerts:               return "SSL loading certs failed";
    case httplib::Error::SSLServerVerification:         return "SSL server cert verification failed";
    case httplib::Error::SSLServerHostnameVerification: return "SSL hostname verification failed";
    case httplib::Error::ProxyConnection:               return "Proxy connection failed";
    default:                                            return "Unknown error";
  }
}

void HttplibTransport::setTimeouts(int connSec, int readSec) {
  this->connSec = connSec;
  this->readSec = readSec;
}

HttpSseResult HttplibTransport::postSse(const std::string& baseUrl, const std::string& path,
                                        const std::string& payload, const std::string& bearerKey,
                                        const SocketHook& onSocket, const Receiver& onChunk) {
  HttpSseResult result;
  httplib::Client cli(baseUrl);
  if (!cli.is_valid()) {
    result.errReason = "invalid url: " + baseUrl;
    return result;
  }
  cli.set_connection_timeout(connSec);
  cli.set_read_timeout(readSec);
  // 捕获 connect 前的裸 socket fd: 中断时主线程对其 shutdown() 解除本线程阻塞的 recv
  // (等首 token / 长 thinking, 回调不跑时回调返回 false 这条路够不到)
  cli.set_socket_options([&onSocket](socket_t s) {
    if (onSocket) onSocket((intptr_t)s);
  });
  httplib::Headers headers;
  if (!bearerKey.empty()) {
    headers.emplace("Authorization", "Bearer " + bearerKey);
  }
  auto res = cli.Post(path, headers, payload, "application/json",
                      [&onChunk](const char* data, size_t len) -> bool {
                        return onChunk ? onChunk(data, len) : true;
                      });
  if (res) {
    result.gotResponse = true;
    result.status = res->status;
  } else {
    result.errReason = httplibErrorString(res.error());
  }
  return result;
}

// ========== C 导出: url 可达性校验 ==========

extern "C" bool vaildHttps(const char* url) {
  if (url == nullptr || url[0] == '\0') return false;
  const std::string full(url);
  // 拆出 baseUrl 与 path: httplib::Client 只接受 scheme://host[:port]。
  const size_t schemeEnd = full.find("://");
  if (schemeEnd == std::string::npos) return false;
  const size_t pathStart = full.find('/', schemeEnd + 3);
  const std::string baseUrl =
      pathStart == std::string::npos ? full : full.substr(0, pathStart);
  const std::string path =
      pathStart == std::string::npos ? std::string("/") : full.substr(pathStart);

  httplib::Client cli(baseUrl);
  if (!cli.is_valid()) return false;
  cli.set_connection_timeout(5);
  cli.set_read_timeout(5);
  cli.set_follow_location(true);

  auto res = cli.Head(path);
  // 有响应即算可达 (含 4xx: 那说明 TLS 与路由都通了, 只是这个路径要鉴权或不支持 HEAD)。
  if (res) return true;
  // 部分服务器不接受 HEAD, 退回 GET 再试一次。
  res = cli.Get(path);
  return static_cast<bool>(res);
}

}
