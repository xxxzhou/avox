#pragma once

// ============================================================================
// vision-toolkit: 视觉模型 HTTP 客户端。
//
// 对齐 dsh-vision-toolkit vendor/agent-vision-toolkit/vision_client.py 的
// chat_completions 协议: 把图片编码成 data:image/<fmt>;base64,<...> 放进
// image_url 内容块, POST 到 {url}{apiPath}, 从 choices[0].message.content 取回文本。
// 底层复用 cpp-httplib (与 HttplibTransport 同源), 不引 Python。
//
// 供应商列表由 ProviderCatalog 派生后传入 (图像理解走 catalog.inputProviders(),
// 文生图走 catalog.genProviders()); 单个端点用 ProviderEntry 表示, 本模块只负责请求。
// ============================================================================

#include <string>
#include <vector>

#include "avox/module/Json.hpp"
#include "avox_agent/adapter/ProviderCatalog.hpp"

namespace avox {

// 单张本地图片 → data URL。扩展名只认 png/jpg/jpeg/webp/gif; 其余返回错误描述(不抛)。
// 成功返回 true 并填 dataUrl。
bool visionImageToDataUrl(const std::string& path, std::string& dataUrl,
                          std::string& error);

// 一次 chat/completions 视觉请求。images 可多张(与文字同轮传入以做对比); text 为提问。
// 遍历 inputProviders, 优先尝试未冷却的端点, 遇 HTTP/网络错误自动加热并切换下一个,
// 直到成功或全部失败 (镜像 FreeModelPool 的失败切换)。
struct VisionChatResult {
  bool ok = false;
  std::string text;   // 模型补全文本; ok=false 时为空
  std::string error;  // ok=false 时的可读错误描述
};

VisionChatResult visionChat(const std::vector<ProviderEntry>& inputProviders,
                            const std::string& text,
                            const std::vector<std::string>& imageDataUrls);

// 便捷: 给定本地图片路径列表 → 一次性调视觉模型。核心是给多图对比(场景2)留一个入口。
VisionChatResult visionChatFiles(const std::vector<ProviderEntry>& inputProviders,
                                 const std::string& text,
                                 const std::vector<std::string>& imagePaths);

// 文生图 (走 genProviders 里端点的 images/generations, 智谱 CogView 等); 没有则返回
// 可读错误。生成的图写入 outPath; outPath 为空时自动生成路径并写回。
struct VisionGenResult {
  bool ok = false;
  std::string path;   // 落盘路径; ok=false 时为空
  std::string error;  // ok=false 时的可读错误描述
};
VisionGenResult visionImageGen(const std::vector<ProviderEntry>& genProviders,
                               const std::string& prompt,
                               const std::string& outPath);

}