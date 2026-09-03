#pragma once

#include "IHttpTransport.hpp"

namespace avox {

// IHttpTransport 的 httplib (cpp-httplib) 实装。生产用。
// 把原散在 AgentClientSSE.cpp 的 httplib::Client 装配 + cli.Post 收敛到此。
class HttplibTransport : public IHttpTransport {
 public:
  void setTimeouts(int connSec, int readSec) override;
  HttpSseResult postSse(const std::string& baseUrl, const std::string& path,
                        const std::string& payload, const std::string& bearerKey,
                        const SocketHook& onSocket, const Receiver& onChunk) override;

 private:
  int connSec = 120;
  int readSec = 120;
};

}
