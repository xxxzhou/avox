#include "ReadImageTool.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ToolArgs.hpp"
#include "ToolIo.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// dsh read_image (#2629 后) 的读侧上限 = min(单图, 单消息聚合) = 3.5 MB:
// 再大的图该先缩, 不是原样入上下文 (仓的 publish 侧同限额再兜一道)。
constexpr uintmax_t kMaxImageBytes = static_cast<uintmax_t>(kImageMaxBytes);

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "file_path": {"type": "string", "description": "图片文件路径 (.png/.jpg/.jpeg/.webp/.gif; 相对路径按会话工作目录解析)"}
    },
    "required": ["file_path"]
  })json";

// dsh read-image 的扩展名映射; 扩展名之外的探测 (魔数) 由附件仓的 publish 做。
std::string mediaTypeForExtension(const std::string& extension) {
  if (extension == ".png") return "image/png";
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".webp") return "image/webp";
  if (extension == ".gif") return "image/gif";
  return std::string();
}

ToolResult executeReadImage(const ToolExecution& exec, AttachmentStore* store) {
  LOGFLF(LogLevel::info, "agent tool read_image");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string rawPath = toolArgString(args, "file_path");
  if (rawPath.empty()) {
    return toolError(ToolOutcome::Fatal, "file_path must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  // 扩展名小写化 (Windows 大小写不敏感, 模型可能给 .PNG)。
  std::string extension = std::filesystem::path(rawPath).extension().string();
  for (char& c : extension) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  const std::string mediaType = mediaTypeForExtension(extension);
  if (mediaType.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "cannot read \"" + rawPath
                         + "\": read_image only accepts PNG/JPEG/WebP/GIF paths",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (store == nullptr) {
    return toolError(ToolOutcome::Fatal,
                     "本部署没有装配附件仓, read_image 不可用",
                     "NO_ATTACHMENT_STORE");
  }

  const std::filesystem::path path = resolveAgentPath(rawPath);
  // 文件级错误与信封引 displayPath (后端解析的绝对路径); 扩展名/装配校验按 dsh 引原始入参。
  const std::string displayPath = path.string();
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return toolError(ToolOutcome::Fatal,
                     "cannot read \"" + displayPath + "\": 文件不存在",
                     "FILE_NOT_READABLE");
  }
  const uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > kMaxImageBytes) {
    return toolError(ToolOutcome::Fatal,
                     "cannot read \"" + displayPath
                         + "\": 文件为空或超过 3.5 MB 单图上限",
                     "FILE_TOO_LARGE");
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  {
    std::ifstream file(path, std::ios::binary);
    if (!file || !file.read(reinterpret_cast<char*>(bytes.data()),
                            static_cast<std::streamsize>(size))) {
      return toolError(ToolOutcome::Fatal,
                       "cannot read \"" + displayPath + "\": 读取失败",
                       "FILE_NOT_READABLE");
    }
  }

  // 入附件仓换内容寻址引用 (嗅探/宽高探测/哈希/原子写都在仓内); 不匹配会抛, 如实转达。
  ImageAttachmentRef ref;
  try {
    ref = store->publish(bytes.data(), bytes.size(), mediaType,
                         path.filename().string());
  } catch (const std::exception& e) {
    return toolError(ToolOutcome::Fatal,
                     std::string("cannot read \"") + displayPath + "\": " + e.what(),
                     "ATTACHMENT_PUBLISH_FAILED");
  }

  // dsh read-image 的信封; 内容块 = 信封文本 + 图片引用 (多模态 Image 块)。
  std::string envelope = "<path>" + displayPath + "</path>\n<type>image</type>\n<content>\n"
      + mediaType + " image, " + std::to_string(ref.width) + "x"
      + std::to_string(ref.height) + " px, " + std::to_string(ref.bytes)
      + " bytes\n</content>";
  ToolResult result;
  result.outcome = ToolOutcome::Ok;
  result.content.push_back(TextBlock{std::move(envelope)});
  result.content.push_back(ImageBlock{ref});
  Json meta(Json::JsonObject{});
  meta["path"] = displayPath;
  Json image(Json::JsonObject{});
  image["attachmentId"] = ref.attachmentId;
  image["mediaType"] = ref.mediaType;
  image["bytes"] = ref.bytes;
  image["width"] = ref.width;
  image["height"] = ref.height;
  if (ref.name.has_value()) image["name"] = *ref.name;
  meta["image"] = std::move(image);
  result.meta = meta.dump();
  return result;
}

std::optional<ToolCallView> presentReadImage(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string path = toolArgString(args, "file_path");
  if (path.empty()) return std::nullopt;
  GenericCallCard card;
  card.kind = ToolCallKind::Read;
  card.title = "Read image " + path;
  card.locations.push_back(FileLocation{path, std::nullopt});
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeReadImageTool(AttachmentStore* attachments) {
  ToolDefinition definition;
  definition.name = "read_image";
  definition.description =
      "读取本地图片 (PNG/JPEG/WebP/GIF, 单图上限 3.5 MB、单边 2000px) 转成视觉输入"
      "块, 供多模态模型直接看图。返回路径信封与图片的尺寸/字节元数据。";
  definition.parametersJson = kParameters;
  // 工厂捕获附件仓裸指针: 仓与工具注册同生命周期 (ComposeAgent.reset 先撤工具
  // 再毁仓), 调用期指针始终有效。
  definition.execute = [attachments](const ToolExecution& exec) -> ToolResult {
    return executeReadImage(exec, attachments);
  };
  definition.timeoutMs = 30000;
  // 纯读: 可与兄弟调用重叠 (dsh read_image 声明 isConcurrencySafe)。
  definition.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  definition.presentCall = presentReadImage;
  return definition;
}

}
