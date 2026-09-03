#include "HttpTranslator.hpp"

#include <chrono>
#include <cstring>
#include <ctime>

#include "ZlmExport.h"
#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/Sha256.hpp"
namespace avox {

// 前向声明 HTTP 回调
static void onTencentHttpResponse(void* user_data, int code,
                                  const char* err_msg);

// ========== HttpTranslator 实现 ==========

HttpTranslator::HttpTranslator() { requester = mk_http_requester_create(); }

HttpTranslator::~HttpTranslator() {
  if (requester) {
    mk_http_requester_release(requester);
    requester = nullptr;
  }
}

bool HttpTranslator::open() {
  LOGFLF(LogLevel::info, "start");
  // 先尝试从配置文件加载密钥
  if (secretId.empty() || secretKey.empty()) {
    loadConfig();
  }
  if (secretId.empty() || secretKey.empty()) {
    lastError = "Tencent cloud credentials not set";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  // 验证密钥有效性：发送一个简单的翻译请求
  if (!sendRequest("test")) {
    lastError = "Failed to send verification request";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  // 等待响应验证
  if (!waitForResponse(2)) {
    LOGFLF(LogLevel::warn,
           "Tencent API verification failed:", lastError.c_str());
    return false;
  }
  // 解析响应验证密钥有效性
  std::string result = parseResponse(responseBody);
  if (result.empty()) {
    LOGFLF(LogLevel::warn,
           "Tencent API verification failed:", lastError.c_str());
    return false;
  }
  loaded = true;
  LOGFLF(LogLevel::info,
         "HttpTranslator loaded with Tencent Cloud API (verified)");
  return true;
}

bool HttpTranslator::loadConfig() {
  // 使用 AssetLoader 加载配置文件
  auto data = AssetLoader::loadToMemory("config/translation.json");
  if (data.empty()) {
    LOGFLF(LogLevel::warn,
           "Failed to load config file: ", AssetLoader::getLastError());
    return false;
  }
  // 解析 JSON
  std::string content(reinterpret_cast<const char*>(data.data()), data.size());
  if (content.empty() || (content[0] != '{' && content[0] != '[')) {
    LOGFLF(LogLevel::warn, "Config content is not valid JSON");
    return false;
  }
  Json config = parserJson(content.c_str());
  if (!config.bObject() || !config.find("tencent")) {
    LOGFLF(LogLevel::warn, "No 'tencent' section in config");
    return false;
  }
  Json tencent = config["tencent"];
  if (!tencent.find("secret_id") || !tencent.find("secret_key")) {
    LOGFLF(LogLevel::warn, "Missing secret_id or secret_key in config");
    return false;
  }
  std::string id = tencent["secret_id"].get<std::string>();
  std::string key = tencent["secret_key"].get<std::string>();
  if (id.empty() || key.empty() || id == "YOUR_SECRET_ID") {
    LOGFLF(LogLevel::warn, "Invalid credentials in config file");
    return false;
  }
  secretId = id;
  secretKey = key;
  LOGFLF(LogLevel::info, "Loaded credentials from config");
  return true;
}

void HttpTranslator::close() { loaded = false; }

void HttpTranslator::setCredentials(const char* id, const char* key) {
  secretId = id ? id : "";
  secretKey = key ? key : "";
}

std::string HttpTranslator::generateSignature(const std::string& payload,
                                              const std::string& timestamp) {
  // 腾讯云 TC3-HMAC-SHA256 签名
  const char* service = "tmt";
  const char* host = "tmt.tencentcloudapi.com";

  // 步骤 1: 拼接规范请求串
  std::string httpRequestMethod = "POST";
  std::string canonicalUri = "/";
  std::string canonicalQueryString = "";
  std::string canonicalHeaders =
      "content-type:application/json\nhost:" + std::string(host) + "\n";
  std::string signedHeaders = "content-type;host";
  std::string hashedRequestPayload = hexEncode(sha256(payload));
  std::string canonicalRequest = httpRequestMethod + "\n" + canonicalUri +
                                 "\n" + canonicalQueryString + "\n" +
                                 canonicalHeaders + "\n" + signedHeaders +
                                 "\n" + hashedRequestPayload;

  // 步骤 2: 拼接待签名字符串
  std::string algorithm = "TC3-HMAC-SHA256";
  // 将时间戳转换为 YYYY-MM-DD 格式 (UTC)
  time_t ts = std::stoll(timestamp);
  struct tm* tm_info = gmtime(&ts);
  char dateBuf[16];
  strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", tm_info);
  std::string date = dateBuf;
  std::string credentialScope = date + "/" + service + "/tc3_request";
  std::string hashedCanonicalRequest = hexEncode(sha256(canonicalRequest));
  std::string stringToSign = algorithm + "\n" + timestamp + "\n" +
                             credentialScope + "\n" + hashedCanonicalRequest;

  // 步骤 3: 计算签名
  std::vector<uint8_t> secretDate = hmacSha256("TC3" + secretKey, date);
  std::vector<uint8_t> secretService = hmacSha256(secretDate, service);
  std::vector<uint8_t> secretSigning = hmacSha256(secretService, "tc3_request");
  std::vector<uint8_t> signature = hmacSha256(secretSigning, stringToSign);

  // 步骤 4: 拼接 Authorization
  std::string authorization = algorithm + " Credential=" + secretId + "/" +
                              credentialScope +
                              ", SignedHeaders=" + signedHeaders +
                              ", Signature=" + hexEncode(signature);
  return authorization;
}

bool HttpTranslator::sendRequest(const std::string& text) {
  // 生成时间戳
  auto now = std::chrono::system_clock::now();
  auto timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();
  std::string timestampStr = std::to_string(timestamp);

  // 构造请求体
  std::string payload = "{\"SourceText\":\"" + text + "\",\"Source\":\"" +
                        sourceLangCode + "\",\"Target\":\"" + targetLangCode +
                        "\",\"ProjectId\":0}";

  // 生成签名
  std::string authorization = generateSignature(payload, timestampStr);

  // 设置请求
  mk_http_requester_set_method(requester, "POST");
  mk_http_requester_add_header(requester, "Content-Type", "application/json",
                               1);
  mk_http_requester_add_header(requester, "Host", "tmt.tencentcloudapi.com", 1);
  mk_http_requester_add_header(requester, "X-TC-Action", "TextTranslate", 1);
  mk_http_requester_add_header(requester, "X-TC-Version", "2018-03-21", 1);
  mk_http_requester_add_header(requester, "X-TC-Timestamp",
                               timestampStr.c_str(), 1);
  mk_http_requester_add_header(requester, "X-TC-Region", "ap-beijing", 1);
  mk_http_requester_add_header(requester, "Authorization",
                               authorization.c_str(), 1);

  mk_http_body body = mk_http_body_from_string(payload.c_str(), payload.size());
  mk_http_requester_set_body(requester, body);
  mk_http_body_release(body);

  // 重置同步状态
  {
    std::lock_guard<std::mutex> lock(responseMutex);
    requestFinished = false;
    responseCode = 0;
    responseBody.clear();
  }

  // 设置回调并发送
  mk_http_requester_set_cb(requester, onTencentHttpResponse, this);
  mk_http_requester_start(requester, "https://tmt.tencentcloudapi.com", 10.0f);

  return true;
}

bool HttpTranslator::waitForResponse(int timeoutSeconds) {
  // 等待响应
  {
    std::unique_lock<std::mutex> lock(responseMutex);
    if (!responseCv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                             [this] { return requestFinished; })) {
      lastError = "Request timeout";
      return false;
    }
  }
  // 检查 HTTP 响应码
  if (responseCode != 0) {
    lastError = "HTTP error: " + std::to_string(responseCode);
    return false;
  }
  return true;
}

std::string HttpTranslator::parseResponse(const std::string& json) {
  if (json.empty() || (json[0] != '{' && json[0] != '[')) {
    lastError = "Invalid response: not JSON";
    LOGFLF(LogLevel::warn, "Invalid response not JSON:", json);
    return "";
  }
  Json root = parserJson(json.c_str());
  if (!root.bObject() || !root.find("Response")) {
    lastError = "Invalid response format";
    return "";
  }

  Json response = root["Response"];

  // 检查错误
  if (response.find("Error")) {
    Json error = response["Error"];
    if (error.find("Message")) {
      lastError = "API Error: " + error["Message"].get<std::string>();
    } else {
      lastError = "API Error";
    }
    return "";
  }

  // 提取 TargetText
  if (response.find("TargetText")) {
    return response["TargetText"].get<std::string>();
  }

  return "";
}

const char* HttpTranslator::translate(const char* text) {
  if (!text || !*text) return "";
  if (!loaded) {
    LOGFLF(LogLevel::warn, "HttpTranslator not loaded");
    return "";
  }
  // 发送请求
  if (!sendRequest(text)) {
    return "";
  }
  // 等待响应
  if (!waitForResponse(10)) {
    LOGFLF(LogLevel::warn, lastError.c_str());
    return "";
  }
  // 解析结果
  resultBuffer = parseResponse(responseBody);
  return resultBuffer.c_str();
}

bool HttpTranslator::ready() const { return loaded; }

// HTTP 回调
static void onTencentHttpResponse(void* user_data, int code,
                                  const char* err_msg) {
  auto* self = static_cast<HttpTranslator*>(user_data);
  if (!self) return;

  std::lock_guard<std::mutex> lock(self->responseMutex);
  self->requestFinished = true;
  self->responseCode = code;

  if (code == 0) {
    size_t body_len = 0;
    const char* body =
        mk_http_requester_get_response_body(self->requester, &body_len);
    if (body && body_len > 0) {
      self->responseBody.assign(body, body_len);
    }
  } else {
    self->lastError = err_msg ? err_msg : "HTTP request failed";
  }

  self->responseCv.notify_one();
}

// HttpTranslator 运行期工厂: 在 AvoxManager::init() 中由 regHttpTranslator()
// 调用, SubtitleAsr 通过 translatorHub.create("http") 查表拿实例(解除核心对
// HttpTranslator 的编译期 include 依赖)
void regHttpTranslator() {
  AvoxManager::Get().translatorHub.reg(
      "http", []() -> BaseTranslator* { return new HttpTranslator(); });
}

// 保存腾讯云 API 凭证(HttpTranslator::loadConfig 运行期读取)
void saveTencentApi(const char* secretId, const char* secretKey) {
  if (!secretId || !secretKey) {
    return;
  }
  std::string json = "{\n  \"tencent\": {\n    \"secret_id\": \"" +
                     std::string(secretId) + "\",\n    \"secret_key\": \"" +
                     std::string(secretKey) + "\"\n  }\n}";
  std::vector<uint8_t> data(json.begin(), json.end());
  AssetLoader::saveToFile("config/translation.json", data);
}

}
