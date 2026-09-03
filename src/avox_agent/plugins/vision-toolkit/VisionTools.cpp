#include "VisionTools.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/ToolTypes.hpp"
#include "avox_agent/tools/ToolArgs.hpp"
#include "VisionHttp.hpp"
#include "VisionImage.hpp"

namespace avox {

// ---------------- 全局供应商快照 (装配期由 registerVisionTools 注入) ----------------
// 视觉工具持有 ProviderCatalog 派生的供应商列表快照, 避免在工具调用期访问目录本体。

struct GlobalVisionCatalog {
  std::vector<ProviderEntry> input;
  std::vector<ProviderEntry> gen;
};

GlobalVisionCatalog& globalVisionCatalog() {
  static GlobalVisionCatalog catalog;
  return catalog;
}

namespace {

// ---------------- 小工具 ----------------

std::atomic<int> gTempCounter{0};

// 生成临时 PNG 路径 (crop 中间产物)。
std::string makeTempPngPath(const std::string& hint) {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path();
  const int n = gTempCounter.fetch_add(1);
  return (dir / ("avox_vision_" + std::to_string(n) + "_" + hint + ".png")).string();
}

std::string colorHex(uint8_t r, uint8_t g, uint8_t b) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return buf;
}

bool parseHexColor(const std::string& text, uint8_t& r, uint8_t& g, uint8_t& b) {
  std::string s = text;
  if (s.size() >= 1 && s[0] == '#') s = s.substr(1);
  if (s.size() != 6) return false;
  auto hex2 = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  auto byte = [&](size_t i) -> int {
    const int hi = hex2(s[i]);
    const int lo = hex2(s[i + 1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 16 + lo;
  };
  const int rv = byte(0), gv = byte(2), bv = byte(4);
  if (rv < 0 || gv < 0 || bv < 0) return false;
  r = static_cast<uint8_t>(rv);
  g = static_cast<uint8_t>(gv);
  b = static_cast<uint8_t>(bv);
  return true;
}

// 把 region 文本转成框并相对图像尺寸收紧; region 为空则铺满全图。
bool resolveRegion(const std::string& regionText, const DecodedImage& img, VisionBox& box,
                   std::string& error) {
  box = VisionBox{0, 0, img.width, img.height};
  if (regionText.empty()) return true;
  VisionBox raw;
  if (!parseVisionBox(regionText, raw)) {
    error = "region 必须是 \"x1,y1,x2,y2\" 形式的像素框, 收到: " + regionText;
    return false;
  }
  raw.x1 = std::clamp(raw.x1, 0, img.width);
  raw.x2 = std::clamp(raw.x2, 0, img.width);
  raw.y1 = std::clamp(raw.y1, 0, img.height);
  raw.y2 = std::clamp(raw.y2, 0, img.height);
  if (raw.x1 > raw.x2) std::swap(raw.x1, raw.x2);
  if (raw.y1 > raw.y2) std::swap(raw.y1, raw.y2);
  if (raw.x2 <= raw.x1 || raw.y2 <= raw.y1) {
    error = "region 在原图内为空: " + regionText;
    return false;
  }
  box = raw;
  return true;
}

// 为视觉请求准备图片的 data URL; region 非空则先本地裁剪再编码。
bool prepareDataUrl(const std::string& path, const std::string& regionText,
                    std::string& dataUrl, std::string& error) {
  if (regionText.empty()) {
    return visionImageToDataUrl(path, dataUrl, error);
  }
  const DecodedImage img = decodeImageToRgb(path);
  if (!img.ok) {
    error = img.error;
    return false;
  }
  VisionBox box;
  if (!resolveRegion(regionText, img, box, error)) return false;
  const std::string temp = makeTempPngPath("crop");
  CropResult crop = cropImageFile(path, box, 1, temp);
  if (!crop.ok) {
    error = "裁剪失败: " + crop.error;
    return false;
  }
  return visionImageToDataUrl(temp, dataUrl, error);
}

ToolDefinition makeBaseDefinition(const char* name, const char* description,
                                  const char* parametersJson, int timeoutMs) {
  ToolDefinition definition;
  definition.name = name;
  definition.description = description;
  definition.parametersJson = parametersJson;
  definition.timeoutMs = timeoutMs;
  return definition;
}

// ---------------- see-image ----------------

ToolResult executeSeeImage(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  if (image.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "see-image 缺少 image 参数 (必填, 图片路径)。", TOOL_CODE_INVALID_ARGS);
  }
  const std::string query = toolArgString(args, "query",
      "请用简体中文详细描述这张图片的内容, 包括关键元素、文字和整体意图。");
  const std::string region = toolArgString(args, "region");
  const std::string compareWith = toolArgString(args, "compareWith");
  const std::string mode = toolArgString(args, "mode", "describe");

  std::vector<std::string> urls;
  std::string error;
  std::string display = image;
  if (!prepareDataUrl(image, (region.empty() && compareWith.empty()) ? region : "", urls.emplace_back(), error)) {
    // prepareDataUrl 需要 region; 无 region 时直接编码整图。
    if (region.empty() && compareWith.empty()) {
      std::string dataUrl;
      if (visionImageToDataUrl(image, dataUrl, error)) {
        urls.back() = dataUrl;
      } else {
        return toolError(ToolOutcome::Fatal, error, "IMAGE_READ_FAILED");
      }
    } else if (!region.empty()) {
      std::string dataUrl;
      if (!prepareDataUrl(image, region, dataUrl, error)) {
        return toolError(ToolOutcome::Fatal, error, "REGION_CROP_FAILED");
      }
      urls.back() = dataUrl;
      display += " (region " + region + ")";
    }
  }
  // 第二张比对图 (场景2): 与第一张同轮发送。
  if (!compareWith.empty()) {
    std::string dataUrl2;
    if (!visionImageToDataUrl(compareWith, dataUrl2, error)) {
      return toolError(ToolOutcome::Fatal, error, "IMAGE_READ_FAILED");
    }
    urls.push_back(std::move(dataUrl2));
  }
  if (urls.empty()) {
    return toolError(ToolOutcome::Fatal, "无法生成图像数据。", "IMAGE_ENCODE_FAILED");
  }

  std::string text = query;
  if (mode == "ocr") {
    text = "请把图中所有可见文字按阅读顺序逐行抄录, 保留换行, 不要评析。";
  } else if (mode == "compare" && !compareWith.empty()) {
    text = "这两张图片: 第一张是 " + image + ", 第二张是 " + compareWith
           + "。请对比它们的内容差异, 用简体中文列出主要区别 (文字、布局、元素、配色等)。";
  }
  const VisionChatResult chat = visionChat(globalVisionCatalog().input, text, urls);
  if (!chat.ok) {
    return toolError(ToolOutcome::Fatal, chat.error, "VISION_REQUEST_FAILED");
  }
  std::string out = chat.text;
  if (!compareWith.empty()) out = "已对比 " + image + " 与 " + compareWith + ":\n" + out;
  return toolOk(std::move(out));
}

constexpr const char* kSeeImageParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "图片路径(必填)。"},
      "query": {"type": "string", "description": "针对性提问; 缺省为详细描述。"},
      "region": {"type": "string", "description": "像素框 x1,y1,x2,y2, 只看该区域。"},
      "compareWith": {"type": "string", "description": "第二张图片路径, 与 image 同轮发送做对比。"},
      "mode": {"type": "string", "enum": ["describe", "ocr", "compare"], "description": "describe=描述/提问, ocr=抄录文字, compare=对比两张图。"}
    },
    "required": ["image"]
  })json";

// ---------------- locate ----------------

// 从模型文本里尽量抽出一个 JSON 数组; 失败返回空。
Json extractJsonArray(const std::string& text) {
  const size_t open = text.find('[');
  if (open == std::string::npos) return Json();
  // 简陋括号匹配: 找到第一个不越界的 ']'。
  int depth = 0;
  size_t close = std::string::npos;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '[') ++depth;
    else if (text[i] == ']') {
      --depth;
      if (depth == 0) {
        close = i + 1;
        break;
      }
    }
  }
  if (close == std::string::npos) return Json();
  try {
    return parserJson(text.substr(open, close - open).c_str());
  } catch (...) {
    return Json();
  }
}

ToolResult executeLocate(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  const std::string target = toolArgString(args, "target");
  if (image.empty() || target.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "locate 缺少 image 或 target 参数 (均必填)。", TOOL_CODE_INVALID_ARGS);
  }
  std::string dataUrl;
  std::string error;
  if (!visionImageToDataUrl(image, dataUrl, error)) {
    return toolError(ToolOutcome::Fatal, error, "IMAGE_READ_FAILED");
  }
  // 拿尺寸填进提示词, 引导坐标用像素。
  const DecodedImage img = decodeImageToRgb(image);
  std::string dims = "unknown";
  if (img.ok) dims = std::to_string(img.width) + "x" + std::to_string(img.height);
  const std::string text =
      "在图中定位目标 \"" + target + "\"。只返回一个 JSON 数组, 每个元素形如 "
      "{\"label\":\"<名称>\",\"box\":{\"x1\":…,\"y1\":…,\"x2\":…,\"y2\":…}}, "
      "坐标是像素, 图片尺寸为 " + dims + ", 图片左上角为(0,0), 右下角为(" + dims + ")。"
      "除 JSON 数组外不要输出任何其他文字。";

  const VisionChatResult chat = visionChat(globalVisionCatalog().input, text, {dataUrl});
  if (!chat.ok) {
    return toolError(ToolOutcome::Fatal, chat.error, "VISION_REQUEST_FAILED");
  }
  const Json raw = extractJsonArray(chat.text);
  if (!raw.bArray()) {
    // 拿不到结构化结果时把模型原文原样带回来, 让模型自取。
    return toolOk("locate 未能解析出结构化坐标, 模型原始回答如下:\n" + chat.text);
  }
  std::string out;
  int count = 0;
  for (size_t i = 0; i < raw.size(); ++i) {
    const Json& item = raw.at(i);
    if (!item.bObject() || !item.find("box") || !item["box"].bObject()) continue;
    const std::string label = item.find("label") ? item["label"].get<std::string>() : ("item" + std::to_string(i + 1));
    const Json& box = item["box"];
    if (!box.find("x1") || !box.find("y1") || !box.find("x2") || !box.find("y2")) continue;
    out += std::to_string(++count) + ". " + label + " x1:"
           + std::to_string(box["x1"].get<int64_t>()) + ", y1:"
           + std::to_string(box["y1"].get<int64_t>()) + ", x2:"
           + std::to_string(box["x2"].get<int64_t>()) + ", y2:"
           + std::to_string(box["y2"].get<int64_t>()) + "\n";
  }
  if (count == 0) {
    out = "没有解析出任何目标框。模型原始回答:\n" + chat.text;
  }
  return toolOk(std::move(out));
}

constexpr const char* kLocateParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "图片路径(必填)。"},
      "target": {"type": "string", "description": "要定位的目标, 如 \"发送按钮\" 或 \"所有输入框\"。"}
    },
    "required": ["image", "target"]
  })json";

// ---------------- ocr-screenshot ----------------

ToolResult executeOcrScreenshot(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  if (image.empty()) {
    return toolError(ToolOutcome::Fatal, "ocr-screenshot 缺少 image 参数(必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  std::string dataUrl;
  std::string error;
  if (!visionImageToDataUrl(image, dataUrl, error)) {
    return toolError(ToolOutcome::Fatal, error, "IMAGE_READ_FAILED");
  }
  const VisionChatResult chat = visionChat(globalVisionCatalog().input,
      "用简体中文把图中所有可见文字(标题、按钮、正文等)按从顶部到底部的阅读顺序逐行抄录, "
      "每行独立, 保留换行与序号, 只输出文字, 不要评析。",
      {dataUrl});
  if (!chat.ok) {
    return toolError(ToolOutcome::Fatal, chat.error, "VISION_REQUEST_FAILED");
  }
  return toolOk(chat.text);
}

constexpr const char* kOcrScreenshotParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "截图路径(必填)。"}
    },
    "required": ["image"]
  })json";

// ---------------- pixel-diff ----------------

ToolResult executePixelDiff(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string original = toolArgString(args, "original");
  const std::string rebuilt = toolArgString(args, "rebuilt");
  if (original.empty() || rebuilt.empty()) {
    return toolError(ToolOutcome::Fatal, "pixel-diff 缺少 original 或 rebuilt 参数(均必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  const int grid = std::clamp(toolArgInt(args, "grid", 6), 1, 64);
  const int top = std::clamp(toolArgInt(args, "top", 5), 1, 32);

  const PixelDiffResult diff = computePixelDiff(original, rebuilt, grid, top);
  if (!diff.ok) {
    return toolError(ToolOutcome::Fatal, diff.error, "PIXEL_DIFF_FAILED");
  }

  // 落盘热力图 + 报告 JSON。
  std::string heatmapPath = rebuilt + ".diff.png";
  std::string error;
  if (!savePngFromRgb(heatmapPath, diff.width, diff.height, diff.heatmap, error)) {
    return toolError(ToolOutcome::Fatal,
                     "写热力图失败: " + error + " (路径 " + heatmapPath + ")", "WRITE_FAILED");
  }

  std::string reportPath = rebuilt + ".diff.json";
  {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"original\": \"" << original << "\",\n";
    ss << "  \"rebuilt\": \"" << rebuilt << "\",\n";
    ss << "  \"scaled\": " << (diff.scaled ? "true" : "false") << ",\n";
    ss << "  \"rebuiltSize\": {\"width\": " << diff.rebuiltWidth
       << ", \"height\": " << diff.rebuiltHeight << "},\n";
    ss << "  \"originalSize\": {\"width\": " << diff.width
       << ", \"height\": " << diff.height << "},\n";
    ss << "  \"overallDifferencePct\": " << diff.overallDifferencePct << ",\n";
    ss << "  \"heatmap\": \"" << heatmapPath << "\",\n";
    ss << "  \"worstRegions\": [\n";
    for (size_t i = 0; i < diff.worst.size(); ++i) {
      const DiffRegion& r = diff.worst[i];
      ss << "    {\"index\": " << r.index << ", \"differencePct\": " << r.differencePct
         << ", \"box\": {\"x1\": " << r.box.x1 << ", \"y1\": " << r.box.y1
         << ", \"x2\": " << r.box.x2 << ", \"y2\": " << r.box.y2 << "}}";
      ss << (i + 1 < diff.worst.size() ? ",\n" : "\n");
    }
    ss << "  ]\n";
    ss << "}\n";
    std::ofstream f(reportPath, std::ios::binary);
    if (f) f << ss.str();
  }

  std::ostringstream out;
  out << "整体差异: " << std::to_string(diff.overallDifferencePct) << "%\n";
  if (diff.scaled) {
    out << "注意: rebuilt 为 " << diff.rebuiltWidth << "x" << diff.rebuiltHeight
        << ", 已缩放到参考图 " << diff.width << "x" << diff.height << " (尺寸不一致本身就是差异)。\n";
  }
  out << "最差区块(从最差到次差):\n";
  for (const DiffRegion& r : diff.worst) {
    out << "  " << r.index << ". " << std::to_string(r.differencePct) << "% x1:"
        << r.box.x1 << ", y1:" << r.box.y1 << ", x2:" << r.box.x2 << ", y2:"
        << r.box.y2 << "\n";
  }
  out << "热力图(红=差异大, 青=差异小): " << heatmapPath << "\n";
  out << "报告 JSON: " << reportPath << "\n";
  out << "提示: 把差异大的区块坐标喂给 see-image 的 region 参数或 locate 定位。";
  return toolOk(out.str());
}

constexpr const char* kPixelDiffParams = R"json({
    "type": "object",
    "properties": {
      "original": {"type": "string", "description": "参考图路径(必填)。"},
      "rebuilt": {"type": "string", "description": "重制/渲染图路径(必填)。"},
      "grid": {"type": "integer", "description": "切成 grid x grid 分块比较, 默认 6。"},
      "top": {"type": "integer", "description": "列出最差区块数, 默认 5。"}
    },
    "required": ["original", "rebuilt"]
  })json";

// ---------------- crop ----------------

ToolResult executeCrop(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  const std::string region = toolArgString(args, "region");
  if (image.empty() || region.empty()) {
    return toolError(ToolOutcome::Fatal, "crop 缺少 image 或 region 参数(均必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  const int scale = std::clamp(toolArgInt(args, "scale", 1), 1, 8);
  std::string output = toolArgString(args, "output");
  if (output.empty()) output = image + ".cropped.png";
  VisionBox box;
  if (!parseVisionBox(region, box)) {
    return toolError(ToolOutcome::Fatal, "region 必须是 \"x1,y1,x2,y2\": " + region,
                     TOOL_CODE_INVALID_ARGS);
  }
  const CropResult crop = cropImageFile(image, box, scale, output);
  if (!crop.ok) {
    return toolError(ToolOutcome::Fatal, crop.error, "CROP_FAILED");
  }
  return toolOk("已裁剪 " + image + " (x,y,w,h 按 region) 到 " + output + ", 尺寸 "
                + std::to_string(crop.width) + "x" + std::to_string(crop.height)
                + (scale > 1 ? " (放大 " + std::to_string(scale) + "x)" : "") + "。");
}

constexpr const char* kCropParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "图片路径(必填)。"},
      "region": {"type": "string", "description": "像素框 x1,y1,x2,y2(必填)。"},
      "scale": {"type": "integer", "description": "放大 1-8 倍, 默认 1。"},
      "output": {"type": "string", "description": "输出路径, 缺省为 <image>.cropped.png。"}
    },
    "required": ["image", "region"]
  })json";

// ---------------- dominant-colors ----------------

struct ColorBucket {
  double sharePct = 0.0;
  uint8_t r = 0, g = 0, b = 0;
};

ToolResult executeDominantColors(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  if (image.empty()) {
    return toolError(ToolOutcome::Fatal, "dominant-colors 缺少 image 参数(必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  const int top = std::clamp(toolArgInt(args, "top", 5), 1, 32);
  const std::string region = toolArgString(args, "region");

  const DecodedImage img = decodeImageToRgb(image);
  if (!img.ok) {
    return toolError(ToolOutcome::Fatal, img.error, "IMAGE_READ_FAILED");
  }
  VisionBox box;
  std::string error;
  if (!resolveRegion(region, img, box, error)) {
    return toolError(ToolOutcome::Fatal, error, TOOL_CODE_INVALID_ARGS);
  }
  const int w = box.x2 - box.x1;
  const int h = box.y2 - box.y1;
  if (w <= 0 || h <= 0) {
    return toolError(ToolOutcome::Fatal, "区域为空。", TOOL_CODE_INVALID_ARGS);
  }

  // 4bit/通道桶: 统计每个量化桶的像素与颜色均值。
  std::map<uint32_t, ColorBucket> buckets;
  uint32_t total = 0;
  const size_t maxSample = 200000;
  const size_t step = std::max<size_t>(1, (static_cast<size_t>(w) * h) / maxSample);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (((static_cast<size_t>(y) * w + x) % step) != 0) continue;
      const uint8_t* px = &img.rgb[(static_cast<size_t>(box.y1 + y) * img.width + box.x1 + x) * 3];
      const uint8_t br = static_cast<uint8_t>((px[0] >> 4) & 0xF);
      const uint8_t bg = static_cast<uint8_t>((px[1] >> 4) & 0xF);
      const uint8_t bb = static_cast<uint8_t>((px[2] >> 4) & 0xF);
      const uint32_t key = (br << 8) | (bg << 4) | bb;
      ColorBucket& b = buckets[key];
      b.sharePct += 1.0;
      b.r += px[0];
      b.g += px[1];
      b.b += px[2];
      ++total;
    }
  }
  if (total == 0) return toolOk("区域无像素。");
  std::vector<ColorBucket> sorted;
  sorted.reserve(buckets.size());
  for (auto& kv : buckets) {
    kv.second.sharePct = kv.second.sharePct / static_cast<double>(total) * 100.0;
    kv.second.r = static_cast<uint8_t>(kv.second.r / (kv.second.sharePct / 100.0 * total) );
    // 上面行化简: 均值 = 累加值 / 像素数。(累加值 / (sharePct/100*total))
    kv.second.g = static_cast<uint8_t>(kv.second.g / (kv.second.sharePct / 100.0 * total));
    kv.second.b = static_cast<uint8_t>(kv.second.b / (kv.second.sharePct / 100.0 * total));
    sorted.push_back(kv.second);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const ColorBucket& lhs, const ColorBucket& rhs) {
              return lhs.sharePct > rhs.sharePct;
            });
  if (static_cast<int>(sorted.size()) > top) sorted.resize(top);

  std::ostringstream out;
  out << "区域 " << box.x1 << "," << box.y1 << "," << box.x2 << "," << box.y2 << " 的显著颜色:\n";
  double shown = 0.0;
  for (const ColorBucket& b : sorted) {
    shown += b.sharePct;
    out << colorHex(b.r, b.g, b.b) << "  " << std::to_string(b.sharePct) << "%\n";
  }
  out << "(前 " << top << " 类合计占比约 " << std::to_string(shown) << "%)";
  return toolOk(out.str());
}

constexpr const char* kDominantColorsParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "图片路径(必填)。"},
      "region": {"type": "string", "description": "像素框 x1,y1,x2,y2, 只看该区域。"},
      "top": {"type": "integer", "description": "返回颜色数, 默认 5。"}
    },
    "required": ["image"]
  })json";

// ---------------- extract-foreground ----------------

ToolResult executeExtractForeground(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string image = toolArgString(args, "image");
  if (image.empty()) {
    return toolError(ToolOutcome::Fatal, "extract-foreground 缺少 image 参数(必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  const std::string region = toolArgString(args, "region");
  const std::string excludeColor = toolArgString(args, "excludeColor");

  const DecodedImage img = decodeImageToRgb(image);
  if (!img.ok) {
    return toolError(ToolOutcome::Fatal, img.error, "IMAGE_READ_FAILED");
  }
  VisionBox box;
  std::string error;
  if (!resolveRegion(region, img, box, error)) {
    return toolError(ToolOutcome::Fatal, error, TOOL_CODE_INVALID_ARGS);
  }
  const int w = box.x2 - box.x1;
  const int h = box.y2 - box.y1;

  auto dist2 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb) {
    const long dr = r - br, dg = g - bg, db = b - bb;
    return dr * dr + dg * dg + db * db;
  };

  // 背景参考色: 参数指定 > 取框边角中位色。
  uint8_t br, bg, bb;
  if (!excludeColor.empty()) {
    if (!parseHexColor(excludeColor, br, bg, bb)) {
      return toolError(ToolOutcome::Fatal, "excludeColor 须为 #RRGGBB: " + excludeColor,
                       TOOL_CODE_INVALID_ARGS);
    }
  } else if (box.x1 < box.x2 && box.y1 < box.y2) {
    const size_t count = 4;
    const int corners[4][2] = {
        {box.x1, box.y1}, {box.x2 - 1, box.y1}, {box.x1, box.y2 - 1}, {box.x2 - 1, box.y2 - 1}};
    long sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < 4; ++i) {
      const uint8_t* px = &img.rgb[(static_cast<size_t>(corners[i][1]) * img.width + corners[i][0]) * 3];
      sr += px[0]; sg += px[1]; sb += px[2];
    }
    br = static_cast<uint8_t>(sr / count);
    bg = static_cast<uint8_t>(sg / count);
    bb = static_cast<uint8_t>(sb / count);
  } else {
    return toolError(ToolOutcome::Fatal, "无法确定背景色: 区域为空。", TOOL_CODE_INVALID_ARGS);
  }
  const long tol2 = 60L * 60L * 3;

  // BFS 连通域标注 (4-邻接)。前景 = 与背景色距离超过 tol2 的像素。
  auto idx = [w](int y, int x) { return static_cast<size_t>(y) * w + x; };
  std::vector<std::pair<int, int>> stack;
  std::vector<uint8_t> fg(static_cast<size_t>(w) * h, 0);
  int maxSeg = 0;
  std::vector<int> segArea;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t* px = &img.rgb[(static_cast<size_t>(box.y1 + y) * img.width + box.x1 + x) * 3];
      if (dist2(px[0], px[1], px[2], br, bg, bb) < tol2) continue;
      if (fg[idx(y, x)]) continue;
      // 新连通域。
      const size_t seg = segArea.size();
      segArea.push_back(0);
      stack.clear();
      stack.emplace_back(x, y);
      fg[idx(y, x)] = static_cast<uint8_t>(1 + seg % 254);
      while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();
        segArea[seg] += 1;
        const int nx[4] = {cx - 1, cx + 1, cx, cx};
        const int ny[4] = {cy, cy, cy - 1, cy + 1};
        for (int d = 0; d < 4; ++d) {
          const int tx = nx[d], ty = ny[d];
          if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
          if (fg[idx(ty, tx)]) continue;
          const uint8_t* qx = &img.rgb[(static_cast<size_t>(box.y1 + ty) * img.width + box.x1 + tx) * 3];
          if (dist2(qx[0], qx[1], qx[2], br, bg, bb) < tol2) continue;
          fg[idx(ty, tx)] = static_cast<uint8_t>(1 + seg % 254);
          stack.emplace_back(tx, ty);
        }
      }
      if (segArea[seg] > maxSeg) maxSeg = segArea[seg];
    }
  }

  // 保留最大连通域及不小于它一半的簇。
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
  size_t kept = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t tag = fg[idx(y, x)];
      if (tag == 0) continue;
      const size_t seg = static_cast<size_t>(tag) - 1;
      if (static_cast<size_t>(maxSeg * 2) >= 1 && segArea[seg] * 2 < maxSeg) continue;
      const uint8_t* px = &img.rgb[(static_cast<size_t>(box.y1 + y) * img.width + box.x1 + x) * 3];
      uint8_t* d = &rgba[idx(y, x) * 4];
      d[0] = px[0]; d[1] = px[1]; d[2] = px[2]; d[3] = 255;
      ++kept;
    }
  }

  std::string output = image + ".fg.png";
  if (!saveRgbaPng(output, w, h, rgba, error)) {
    return toolError(ToolOutcome::Fatal, "写前景 PNG 失败: " + error, "WRITE_FAILED");
  }
  const double pct = static_cast<double>(kept) / (static_cast<double>(w) * h) * 100.0;
  std::string out = "已抠出前景, 保留像素 " + std::to_string(kept) + " 个 (" + std::to_string(pct)
                    + "%), 背景色#" + colorHex(br, bg, bb) + ", 输出 " + output + "\n";
  return toolOk(std::move(out));
}

constexpr const char* kExtractForegroundParams = R"json({
    "type": "object",
    "properties": {
      "image": {"type": "string", "description": "图片路径(必填)。"},
      "region": {"type": "string", "description": "限制抠图范围, 像素框 x1,y1,x2,y2。"},
      "excludeColor": {"type": "string", "description": "背景色 #RRGGBB; 缺省取区域四角中位色。"}
    },
    "required": ["image"]
  })json";

// ---------------- generate-image ----------------

ToolResult executeGenerateImage(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string prompt = toolArgString(args, "prompt");
  if (prompt.empty()) {
    return toolError(ToolOutcome::Fatal, "generate-image 缺少 prompt 参数(必填)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  const std::string output = toolArgString(args, "output");
  const VisionGenResult gen = visionImageGen(globalVisionCatalog().gen, prompt, output);
  if (!gen.ok) {
    return toolError(ToolOutcome::Fatal, gen.error, "IMAGE_GEN_FAILED");
  }
  return toolOk("已生成图片并保存到: " + gen.path);
}

constexpr const char* kGenerateImageParams = R"json({
    "type": "object",
    "properties": {
      "prompt": {"type": "string", "description": "要生成的画面描述(必填)。"},
      "output": {"type": "string", "description": "输出路径, 缺省 avox_generated.png。"}
    },
    "required": ["prompt"]
  })json";

}  // namespace

// ---------------- 工厂与注册 ----------------

ToolDefinition makeSeeImageTool(bool conversationImageInput) {
  // 多模态主模型下, 把"通用看图 + OCR"都导给 read_image, 本工具只描述专项用途;
  // 纯文本主模型下, see-image 是唯一看图入口, 描述维持强引导原状。
  const char* description = conversationImageInput
      ? "视觉大模型专项工具: 双图对比、按区域描述等 read_image 不便做的任务。"
        "图片路径必须在本会话工作区或临时目录内。返回的是文字结论, 不是坐标; 需要坐标用 locate。"
        "注意: 通用「看图」与 OCR 抄录请都用 read_image (本次会话主模型支持图片输入, "
        "图片直接进上下文, 主模型既能看图也能 OCR, 比绕视觉模型更准更省);"
        "本工具 (含 mode=ocr) 是为纯文本主模型准备的, 多模态主模型下走本工具即绕路。"
      : "让视觉大模型理解一张图: 描述内容/回答针对性问题/抄录文字/对比两张图(场景1、场景2)。"
        "图片路径必须在本会话工作区或临时目录内。返回的是文字结论, 不是坐标; 需要坐标用 locate。"
        "注意: 必须用本工具看图并返回文字, 而不是把图片路径交给主对话模型 —— 主模型是纯文本的, "
        "不认图片 (read_image 仅在多模态主模型时可用)。";
  ToolDefinition def = makeBaseDefinition("see-image", description, kSeeImageParams, 180000);
  def.execute = executeSeeImage;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeLocateTool() {
  ToolDefinition def = makeBaseDefinition("locate",
      "让视觉大模型在图中定位目标元素, 返回像素框 (x1,y1,x2,y2)。框可直接喂给 crop / see-image 的 region 参数。",
      kLocateParams, 180000);
  def.execute = executeLocate;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeOcrScreenshotTool(bool conversationImageInput) {
  // 多模态主模型下, 通用 OCR 抄录应该走 read_image (主模型直接看图, 还能结合上下文);
  // 本工具只留给纯文本主模型场景, 或确实需要外部视觉模型的特定需求。
  const char* description = conversationImageInput
      ? "走视觉大模型的 OCR 工具, 适合纯文本主模型场景。多模态主模型下通用 OCR 请直接用 "
        "read_image (主模型既能看图也能 OCR, 比绕视觉模型更准更省)。"
      : "把截图里所有可见文字按阅读顺序抄录出来 (走视觉大模型)。适合文字较少的界面截图, "
        "纯文本长屏用自带的 OCR 工具更省。";
  ToolDefinition def = makeBaseDefinition("ocr-screenshot", description,
                                          kOcrScreenshotParams, 180000);
  def.execute = executeOcrScreenshot;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makePixelDiffTool() {
  ToolDefinition def = makeBaseDefinition("pixel-diff",
      "逐像素对比两张图并给出整体差异百分比与最差区块 (本地确定性计算), 产出热力图与 JSON 报告。"
      "用于验证对图的改动落地了多少(场景2): 先给参考图 original 和改动后的 rebuilt。",
      kPixelDiffParams, 60000);
  def.execute = executePixelDiff;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeCropTool() {
  ToolDefinition def = makeBaseDefinition("crop",
      "本地裁剪图像的指定像素区域(可放大), 无需视觉密钥。裁剪前通常先 locate 拿到框。",
      kCropParams, 30000);
  def.execute = executeCrop;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeDominantColorsTool() {
  ToolDefinition def = makeBaseDefinition("dominant-colors",
      "统计图像(或区域)的显著颜色及占比。用于判断配色、主题是否一致。",
      kDominantColorsParams, 30000);
  def.execute = executeDominantColors;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeExtractForegroundTool() {
  ToolDefinition def = makeBaseDefinition("extract-foreground",
      "把图像前景(图标/Logo)从背景里抠出来, 输出透明 PNG。可指定背景色。",
      kExtractForegroundParams, 30000);
  def.execute = executeExtractForeground;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

ToolDefinition makeGenerateImageTool() {
  ToolDefinition def = makeBaseDefinition("generate-image",
      "文生图: 用 ProviderCatalog 里 imageOutput 的端点 (智谱 CogView 等) 依据 prompt 生成一张图片并保存。"
      "需要 agent.json 里配了 imageOutput=true 的节点且填了有效 apiKey。",
      kGenerateImageParams, 180000);
  def.execute = executeGenerateImage;
  def.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return def;
}

std::vector<Disposer> registerVisionTools(AgentHost& host, const ProviderCatalog& catalog,
                                          bool conversationImageInput) {
  globalVisionCatalog().input = catalog.inputProviders();
  globalVisionCatalog().gen = catalog.genProviders();
  std::vector<Disposer> disposers;
  const auto define = [&](ToolDefinition definition) {
    try {
      disposers.push_back(host.defineTool(std::move(definition)));
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[vision-toolkit] 注册工具失败: ", e.what());
    }
  };
  define(makeSeeImageTool(conversationImageInput));
  define(makeLocateTool());
  define(makeOcrScreenshotTool(conversationImageInput));
  define(makePixelDiffTool());
  define(makeCropTool());
  define(makeDominantColorsTool());
  define(makeExtractForegroundTool());
  define(makeGenerateImageTool());
  return disposers;
}

}