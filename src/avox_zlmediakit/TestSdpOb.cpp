#include "TestSdpOb.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// C回调函数(与 TestSdpOb.hpp 的 friend 声明对应, 不能加 static)
void onTestHttpComplete(void* user_data, int code, const char* err_msg) {
  auto* self = static_cast<TestSdpOb*>(user_data);
  if (!self) return;

  if (code != 0) {
    log(LogLevel::warn, "sdp http request failed: ", err_msg ? err_msg : "unknown");
    return;
  }

  size_t body_len = 0;
  const char* body = mk_http_requester_get_response_body(self->requester, &body_len);
  if (!body || body_len == 0) {
    log(LogLevel::warn, "sdp http empty response");
    return;
  }

  std::string content(body, body_len);
  Json jcontent = parserJson(content.c_str());

  int64_t resp_code = jcontent["code"];
  if (resp_code != 0) {
    std::string msg = jcontent["msg"];
    log(LogLevel::warn, "sdp http response code:", resp_code, " msg:", msg);
    return;
  }

  std::string sdp = jcontent["sdp"];
  log(LogLevel::info, "sdp http response sdp:", sdp);

  if (self->player) {
    self->player->setRemoteSdp(sdp.c_str());
  }
}

TestSdpOb::TestSdpOb(const char* serverUrl) {
  server = serverUrl;
  requester = mk_http_requester_create();
}

TestSdpOb::~TestSdpOb() {
  if (requester) {
    mk_http_requester_release(requester);
    requester = nullptr;
  }
}

void TestSdpOb::onLocalSdp(const char* sdp) {
  LOGFLF(LogLevel::info, "server:", server, " local sdp:", sdp);

  mk_http_requester_set_method(requester, "POST");
  mk_http_requester_add_header(requester, "Content-Type", "text/plain;charset=utf-8", 1);
  mk_http_requester_add_header(requester, "User-Agent", "Mozilla/5.0", 1);
  mk_http_requester_add_header(requester, "Accept", "*/*", 1);

  // 创建body
  mk_http_body body = mk_http_body_from_string(sdp, strlen(sdp));
  mk_http_requester_set_body(requester, body);
  mk_http_body_release(body);

  mk_http_requester_set_cb(requester, onTestHttpComplete, this);
  mk_http_requester_start(requester, server.c_str(), 10.0f);
}

}
