#include "VisionHttp.hpp"
#include "VisionImage.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "avox/module/Sha256.hpp"  // base64Encode

#define CPPHTTPLIB_NO_EXCEPTIONS
#include "httplib.h"

namespace avox {

namespace {

// JSON 字符串转义: 引号/反斜杠/控制符。模型名与提问文本都走它, 防止非法 JSON 把整包打挂。
std::string escapeJsonString(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string readFileBytes(const std::string& path, std::string& fileError) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    fileError = "无法读取文件: " + path;
    return std::string();
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

// 扩展名 → MIME (只有视觉模型支持的几种)。返回空串表示不支持。
std::string mimeByExtension(const std::string& path) {
  std::string ext;
  const size_t dot = path.rfind('.');
  if (dot != std::string::npos) ext = path.substr(dot + 1);
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "webp") return "image/webp";
  if (ext == "gif") return "image/gif";
  return std::string();
}

std::string httplibErrorText(httplib::Error err) {
  switch (err) {
    case httplib::Error::Connection:                    return "连接失败";
    case httplib::Error::ConnectionTimeout:             return "连接超时";
    case httplib::Error::ConnectionClosed:              return "连接被关闭";
    case httplib::Error::Read:                          return "读取错误";
    case httplib::Error::Write:                         return "写入错误";
    case httplib::Error::Timeout:                       return "读取超时";
    case httplib::Error::SSLConnection:                 return "SSL 连接失败";
    case httplib::Error::SSLLoadingCerts:               return "SSL 证书加载失败";
    case httplib::Error::SSLServerVerification:         return "SSL 服务器证书校验失败";
    case httplib::Error::SSLServerHostnameVerification: return "SSL 主机名校验失败";
    case httplib::Error::ProxyConnection:               return "代理连接失败";
    default:                                            return "未知网络错误";
  }
}

// 从 chat/completions 响应里抽取纯文本 (兼容 content 是字符串或内容块数组两种形态)。
std::string extractMessageText(const Json& root) {
  if (!root.bObject() || !root.find("choices")) return std::string();
  const Json& choices = root["choices"];
  if (!choices.bArray() || choices.size() == 0) return std::string();
  const Json& first = choices.at(0);
  if (!first.bObject() || !first.find("message")) return std::string();
  const Json& message = first["message"];
  if (!message.bObject() || !message.find("content")) return std::string();
  const Json& content = message["content"];
  if (content.bString()) return content.get<std::string>();
  if (content.bArray()) {
    std::string joined;
    for (size_t i = 0; i < content.size(); ++i) {
      const Json& part = content.at(i);
      if (part.bObject() && part.find("text") && part["text"].bString()) {
        if (!joined.empty()) joined += '\n';
        joined += part["text"].get<std::string>();
      }
    }
    return joined;
  }
  return std::string();
}

// ---- 失败切换: 各 provider 共用的「加热/冷却」状态 (镜像 FreeModelPool) ----
// 身份用 provider.id()(url+model), 与 providers 数组顺序无关; 跨请求持续, 冷却到期自愈。

constexpr int kVisionCooldownSec = 15;  // 失败后冷却窗口, 到期自动恢复尝试

struct HeatedEntry { std::chrono::steady_clock::time_point until; };

std::map<std::string, HeatedEntry>& heatedProviders() {
  static std::map<std::string, HeatedEntry> map;
  return map;
}

// 某 provider 当前是否处于冷却中 (过期项顺手清除)。
bool isHeated(const std::string& id) {
  std::map<std::string, HeatedEntry>& map = heatedProviders();
  const auto it = map.find(id);
  if (it == map.end()) return false;
  if (std::chrono::steady_clock::now() >= it->second.until) {
    map.erase(it);
    return false;
  }
  return true;
}

void heat(const std::string& id) {
  heatedProviders()[id] = {std::chrono::steady_clock::now()
                           + std::chrono::seconds(kVisionCooldownSec)};
}

// 错误与日志里的可读 provider 标签。
std::string providerLabel(const ProviderEntry& provider) {
  return provider.name.empty() ? provider.apiUrl
                               : provider.name + " (" + provider.apiUrl + ")";
}

// 视觉端点解码上限约 4MB。原始字节超过它时自动降采样, 避免 HTTP 413。
constexpr size_t kMaxDecodedBytes = 4 * 1024 * 1024;
// 降采样后的长边目标 (1280x720 量级 JPEG 通常 <500KB, 稳过 4MB 上限且省积分)。
constexpr int kMaxLongEdge = 1280;

std::atomic<int> gDownsampleCounter{0};

// 生成降采样临时 JPEG 路径 (std 临时目录, 用完即删)。
std::string makeTempJpgPath() {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path();
  const int n = gDownsampleCounter.fetch_add(1);
  return (dir / ("avox_vision_down_" + std::to_string(n) + ".jpg")).string();
}

// 图片解码后字节是否超端点上限 (约等于原始文件字节, 只有我们已知文件是 base64 的来源)。
bool exceedsImageSizeLimit(const std::string& path) {
  std::string fileError;
  const std::string raw = readFileBytes(path, fileError);
  return !raw.empty() && raw.size() > kMaxDecodedBytes;
}

// ---- 文生图用: 简单的 base64 解码 (avox 只有 encode) ----
// 只支持标准字符集, 容忍结尾补齐 '='。失败返回空串。
std::vector<uint8_t> base64Decode(const std::string& data) {
  auto unmap = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  int value = 0, bits = 0;
  for (const char raw : data) {
    if (raw == '=') break;
    if (raw == '\n' || raw == '\r' || raw == ' ') continue;
    const int digit = unmap(raw);
    if (digit < 0) return std::vector<uint8_t>();
    value = (value << 6) | digit;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
    }
  }
  return out;
}

}  // namespace

bool visionImageToDataUrl(const std::string& path, std::string& dataUrl,
                          std::string& error) {
  const std::string mime = mimeByExtension(path);
  if (mime.empty()) {
    error = "仅支持 PNG/JPEG/WebP/GIF 图像: " + path;
    return false;
  }
  // 图片解码字节超上限 (如 anionex 4MB): 自动降采样重编码成 JPEG, 避免 HTTP 413,
  // 也省积分。降采样失败时退回原图 (让端点自行决定)。
  if (exceedsImageSizeLimit(path)) {
    const DecodedImage img = decodeImageToRgb(path);
    if (img.ok && img.width > 0 && img.height > 0) {
      const double scale =
          std::max(img.width, img.height) > kMaxLongEdge
              ? static_cast<double>(kMaxLongEdge) / std::max(img.width, img.height)
              : 1.0;
      if (scale < 1.0) {
        const int outW = std::max(1, static_cast<int>(img.width * scale));
        const int outH = std::max(1, static_cast<int>(img.height * scale));
        std::vector<uint8_t> down;
        resizeRgbBilinear(img.rgb, img.width, img.height, down, outW, outH);
        const std::string temp = makeTempJpgPath();
        std::string saveError;
        if (saveRgbImageFile(temp, outW, outH, down, saveError)) {
          std::string readError;
          const std::string reencoded = readFileBytes(temp, readError);
          std::remove(temp.c_str());
          if (!reencoded.empty()) {
            dataUrl = "data:image/jpeg;base64," +
                      base64Encode(std::vector<uint8_t>(reencoded.begin(), reencoded.end()));
            return true;
          }
        }
        std::remove(temp.c_str());
      }
    }
  }
  std::string fileError;
  const std::string raw = readFileBytes(path, fileError);
  if (raw.empty()) {
    error = fileError.empty() ? ("图像为空: " + path) : fileError;
    return false;
  }
  const std::vector<uint8_t> bytes(raw.begin(), raw.end());
  dataUrl = "data:" + mime + ";base64," + base64Encode(bytes);
  return true;
}

// 单个 provider 的一次视觉请求 (含单图模型的多图退化)。
VisionChatResult chatWithProvider(const ProviderEntry& provider,
                                  const std::string& text,
                                  const std::vector<std::string>& imageDataUrls) {
  VisionChatResult result;

  // 单次 chat/completions 请求 (可带多张 image_url 内容块)。多图退化为逐张时也复用它。
  auto postOnce = [&](const std::string& prompt,
                      const std::vector<std::string>& urls) -> VisionChatResult {
    VisionChatResult single;
    std::string body = "{\"model\":\"" + escapeJsonString(provider.model)
                       + "\",\"messages\":[{\"role\":\"user\",\"content\":[";
    body += "{\"type\":\"text\",\"text\":\"" + escapeJsonString(prompt) + "\"}";
    for (const std::string& url : urls) {
      body += ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"" + escapeJsonString(url)
              + "\"}}";
    }
    body += "]}]}";

    httplib::Client cli(provider.apiUrl);
    if (!cli.is_valid()) {
      single.ok = false;
      single.error = "视觉模型基址非法: " + provider.apiUrl;
      return single;
    }
    cli.set_connection_timeout(provider.timeoutSec);
    cli.set_read_timeout(provider.timeoutSec);
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer " + provider.apiKey);
    const auto res = cli.Post(provider.apiPath, headers, body, "application/json");
    if (!res) {
      single.ok = false;
      single.error = httplibErrorText(res.error());
      return single;
    }
    if (res->status < 200 || res->status >= 300) {
      // 优先取上游 error.message 给模型看清楚; 拿不到再退回状态码。
      std::string upstream;
      try {
        const Json root = parserJson(res->body.c_str());
        if (root.bObject() && root.find("error") && root["error"].bObject()
            && root["error"].find("message")) {
          upstream = root["error"]["message"].get<std::string>();
        }
      } catch (...) {
      }
      single.ok = false;
      single.error = "HTTP " + std::to_string(res->status)
                     + (upstream.empty() ? "" : ": " + upstream);
      return single;
    }

    try {
      const Json root = parserJson(res->body.c_str());
      const std::string textOut = extractMessageText(root);
      if (textOut.empty()) {
        single.ok = false;
        single.error = "返回了空或无法解析的内容";
        return single;
      }
      single.ok = true;
      single.text = textOut;
      return single;
    } catch (const std::exception& e) {
      single.ok = false;
      single.error = std::string("返回非法 JSON: ") + e.what();
      return single;
    }
  };

  // 支持多图或只有一张图: 一次性整包请求。
  if (provider.multiImage || imageDataUrls.size() <= 1) {
    return postOnce(text, imageDataUrls);
  }

  // 单图模型 (如 GLM-4V-Flash) + 多图: 自动退化为逐张调用, 最后把每张的结论拼接给上层。
  std::vector<std::string> parts;
  parts.reserve(imageDataUrls.size());
  int successCount = 0;
  for (size_t i = 0; i < imageDataUrls.size(); ++i) {
    const std::string prompt = text + "\n(请只针对给出的第 " + std::to_string(i + 1)
                               + " 张图回答)";
    const VisionChatResult one = postOnce(prompt, {imageDataUrls[i]});
    std::string part = "\u3010图 " + std::to_string(i + 1) + "\u3011";
    if (one.ok) {
      ++successCount;
      part += " " + one.text;
    } else {
      part += " 识别失败: " + one.error;
    }
    parts.push_back(std::move(part));
  }

  std::string merged;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) merged += "\n";
    merged += parts[i];
  }
  if (successCount == 0) {
    result.ok = false;
    result.error = "逐张识别均失败: " + merged;
    return result;
  }
  result.ok = true;
  result.text = merged;
  return result;
}

VisionChatResult visionChat(const std::vector<ProviderEntry>& inputProviders,
                            const std::string& text,
                            const std::vector<std::string>& imageDataUrls) {
  VisionChatResult result;
  if (imageDataUrls.empty()) {
    result.ok = false;
    result.error = "视觉请求必须至少带一张图片。";
    return result;
  }

  // 候选: 未冷却的优先 (好用先用), 冷却中的留作最后兜底重试。
  std::vector<const ProviderEntry*> fresh;
  std::vector<const ProviderEntry*> heated;
  for (const ProviderEntry& provider : inputProviders) {
    if (!provider.ready()) continue;
    if (isHeated(provider.id())) heated.push_back(&provider);
    else fresh.push_back(&provider);
  }
  if (fresh.empty() && heated.empty()) {
    result.ok = false;
    result.error =
        "视觉模型未配置完整: 需在 agent.json 里给某个模型节点置 imageInput=true 并填"
        "有效 apiKey (占位符 YOUR_* 会被视为未配置)。";
    return result;
  }

  // 逐个尝试, 失败加热并切下一个; 任一成功即返回。
  auto attempt = [&](const ProviderEntry* provider) -> VisionChatResult {
    VisionChatResult r = chatWithProvider(*provider, text, imageDataUrls);
    if (r.ok) return r;
    heat(provider->id());
    return r;
  };
  std::vector<std::string> errors;
  for (const ProviderEntry* provider : fresh) {
    VisionChatResult r = attempt(provider);
    if (r.ok) return r;
    errors.push_back(providerLabel(*provider) + ": " + r.error);
  }
  for (const ProviderEntry* provider : heated) {
    VisionChatResult r = attempt(provider);
    if (r.ok) return r;
    errors.push_back(providerLabel(*provider) + ": " + r.error);
  }

  result.ok = false;
  result.error = "所有视觉端点均失败: ";
  for (size_t i = 0; i < errors.size(); ++i) {
    result.error += (i ? " | " : "") + errors[i];
  }
  return result;
}

VisionChatResult visionChatFiles(const std::vector<ProviderEntry>& inputProviders,
                                 const std::string& text,
                                 const std::vector<std::string>& imagePaths) {
  std::vector<std::string> urls;
  urls.reserve(imagePaths.size());
  for (const std::string& path : imagePaths) {
    std::string dataUrl;
    std::string error;
    if (!visionImageToDataUrl(path, dataUrl, error)) {
      VisionChatResult fail;
      fail.ok = false;
      fail.error = error;
      return fail;
    }
    urls.push_back(std::move(dataUrl));
  }
  return visionChat(inputProviders, text, urls);
}

VisionGenResult visionImageGen(const std::vector<ProviderEntry>& genProviders,
                               const std::string& prompt,
                               const std::string& outPath) {
  VisionGenResult result;
  const ProviderEntry* genProvider = nullptr;
  for (const ProviderEntry& provider : genProviders) {
    if (provider.ready()) { genProvider = &provider; break; }
  }
  if (!genProvider) {
    result.error =
        "没有可用的文生图端点: 需在 agent.json 里给某个模型节点置 imageOutput=true 并填"
        "有效 apiKey (占位符 YOUR_* 会被视为未配置)。可用模型如 cogview-3-flash 系列。";
    return result;
  }

  // 组装 images/generations 端点路径: 在基址的路径前缀后追加。
  std::string cliHost = genProvider->apiUrl;
  std::string endPath = "/images/generations";
  const size_t scheme = genProvider->apiUrl.find("://");
  const size_t hostEnd = genProvider->apiUrl.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  if (hostEnd != std::string::npos) {
    cliHost = genProvider->apiUrl.substr(0, hostEnd);
    endPath = genProvider->apiUrl.substr(hostEnd) + "/images/generations";
  }

  const std::string body = "{\"model\":\"" + escapeJsonString(genProvider->model)
                           + "\",\"prompt\":\"" + escapeJsonString(prompt)
                           + "\",\"size\":\"1024x1024\"}";
  httplib::Client cli(cliHost);
  if (!cli.is_valid()) {
    result.error = "文生图基址非法: " + genProvider->apiUrl;
    return result;
  }
  cli.set_connection_timeout(genProvider->timeoutSec);
  cli.set_read_timeout(genProvider->timeoutSec);
  httplib::Headers headers;
  headers.emplace("Authorization", "Bearer " + genProvider->apiKey);
  const auto res = cli.Post(endPath, headers, body, "application/json");
  if (!res) {
    result.error = "文生图请求失败(" + providerLabel(*genProvider) + "): "
                   + httplibErrorText(res.error());
    return result;
  }
  if (res->status < 200 || res->status >= 300) {
    result.error = "文生图 HTTP " + std::to_string(res->status) + " (" + providerLabel(*genProvider) + "): "
                   + res->body;
    return result;
  }

  // 解析结果: 优先 data[0].b64_json, 其次 data[0].url。
  std::vector<uint8_t> imageBytes;
  try {
    const Json root = parserJson(res->body.c_str());
    if (root.bObject() && root.find("data") && root["data"].bArray()
        && root["data"].size() > 0) {
      const Json& first = root["data"].at(0);
      if (first.bObject() && first.find("b64_json") && first["b64_json"].bString()) {
        imageBytes = base64Decode(first["b64_json"].get<std::string>());
      }
    }
  } catch (...) {
  }
  if (!imageBytes.empty()) {
    const std::string path = outPath.empty() ? "avox_generated.png" : outPath;
    std::ofstream file(path, std::ios::binary);
    if (!file) {
      result.error = "写文生图结果失败: " + path;
      return result;
    }
    file.write(reinterpret_cast<const char*>(imageBytes.data()),
               static_cast<std::streamsize>(imageBytes.size()));
    result.ok = true;
    result.path = path;
    return result;
  }

  // 没有 b64: 尝试取返回的 url 落盘 (CogView 可能返回图片地址)。
  std::string imageUrl;
  try {
    const Json root = parserJson(res->body.c_str());
    if (root.bObject() && root.find("data") && root["data"].bArray()
        && root["data"].size() > 0) {
      const Json& first = root["data"].at(0);
      if (first.bObject() && first.find("url") && first["url"].bString()) {
        imageUrl = first["url"].get<std::string>();
      }
    }
  } catch (...) {
  }
  if (imageUrl.empty()) {
    result.error = "文生图响应无法解析出图片: " + res->body;
    return result;
  }
  const std::string path = outPath.empty() ? "avox_generated.png" : outPath;
  httplib::Client dl(imageUrl);
  if (!dl.is_valid()) {
    result.error = "文生图下载地址非法: " + imageUrl;
    return result;
  }
  const auto image = dl.Get("/");
  if (!image || image->status < 200 || image->status >= 300) {
    result.error = "文生图下载失败: " + imageUrl;
    return result;
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    result.error = "写文生图结果失败: " + path;
    return result;
  }
  file.write(reinterpret_cast<const char*>(image->body.data()),
             static_cast<std::streamsize>(image->body.size()));
  result.ok = true;
  result.path = path;
  return result;
}

}