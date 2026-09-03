#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

#include "avox/subtitle/BaseTranslator.hpp"
#include "avox/module/LogHelper.hpp"
#include "mk_httpclient.h"


namespace avox {

/**
 * @brief HTTP 云翻译器（腾讯云翻译 API）
 * https://cloud.tencent.com/document/product/551/15619
 *
 * 使用 ZLMediaKit 的 HTTP 客户端发送翻译请求
 * 仅在 AVOX_ENABLE_ZLMEDIAKIT 编译时可用
 */

class HttpTranslator : public BaseTranslator {
 public:
  HttpTranslator();
  ~HttpTranslator();

 private:
  mk_http_requester requester = nullptr;
  std::string secretId;
  std::string secretKey;
  bool loaded = false;

  // 异步请求同步等待
  std::mutex responseMutex;
  std::condition_variable responseCv;
  bool requestFinished = false;
  int responseCode = 0;
  std::string responseBody;

 public:
  bool open() override;
  void close() override;
  const char* translate(const char* text) override;
  bool ready() const override;

  // 设置腾讯云密钥
  void setCredentials(const char* secretId, const char* secretKey);

  // TC3-HMAC-SHA256 签名
  std::string generateSignature(const std::string& payload,
                                const std::string& timestamp);

  // 发送翻译请求
  bool sendRequest(const std::string& text);

  // 解析翻译结果
  std::string parseResponse(const std::string& json);

  // 等待响应，返回 true 表示成功
  bool waitForResponse(int timeoutSeconds);

  // 从配置文件加载密钥
  bool loadConfig();

  // HTTP 回调
  friend void onTencentHttpResponse(void* user_data, int code,
                                    const char* err_msg);
};

}
