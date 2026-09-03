#pragma once

#include <functional>
#include <string>

#include "avox/AvoxDef.h"  // AVOX_START/END_NAMESPACE

namespace avox {

// SSE POST 结果: 区分连接失败 / HTTP 状态 / 错误串
// (对应原 cli.Post 返回的 !res / res / res->status 三分支)
struct HttpSseResult {
  bool gotResponse = false;   // false = 连接失败 (原 !res 分支)
  int status = 0;             // HTTP 状态码 (gotResponse=true 时有效)
  std::string errReason;      // !gotResponse 时的 httplib 错误串
};

// HTTP 传输 seam: 把 httplib 收敛到单一编译单元 (HttplibTransport), 支持注入 Mock 离线测试。
// LlmProviderAdapter 持有 IHttpTransport*, 生产环境用 HttplibTransport, 测试注入 MockTransport。
class IHttpTransport {
 public:
  virtual ~IHttpTransport() = default;
  // SSE chunk 接收回调: 返回 false 立即停止读 (中断用)
  using Receiver = std::function<bool(const char* data, size_t len)>;
  // socket 注册回调: connect 前捕获裸 fd (中断时 shutdown 解除阻塞 recv)
  using SocketHook = std::function<void(intptr_t s)>;

  // 设置连接/读超时 (秒), 请求前调
  virtual void setTimeouts(int connSec, int readSec) = 0;
  // SSE POST: 首轮 + 工具循环 round-2 共用。baseUrl=scheme://host:port, path=请求路径,
  // payload=请求体, bearerKey=apiKey(空则不带 Authorization), onSocket/onChunk 见上。
  virtual HttpSseResult postSse(const std::string& baseUrl, const std::string& path,
                                const std::string& payload, const std::string& bearerKey,
                                const SocketHook& onSocket, const Receiver& onChunk) = 0;
};

extern "C" {
// 校验一个 https(或 http) url 是否可达 (建连 + 一次 HEAD/GET)。
//
// 经 avox.dll 的 C 导出提供: 外部 exe 不能自己 include httplib —— 那会让 exe 与 avox.dll
// 各有一份 httplib 静态状态, OpenSSL 重复初始化会崩。
AVOX_EXPORT bool vaildHttps(const char* url);
}

}
