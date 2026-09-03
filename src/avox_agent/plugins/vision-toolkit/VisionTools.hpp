#pragma once

// ============================================================================
// vision-toolkit: 工具定义。
//
// 对齐 dsh-vision-toolkit 的工具集 (去除依赖无头 Chrome / vtracer 外部二进制的
// html_screenshot、trace), 其余尽量移植:
//   see-image         → vision_glance         (让文本模型理解图像/比对, 场景1/2)
//   pixel-diff        → vision_pixel_diff     (两图逐像素差异 + 热力图, 场景2)
//   locate            → vision_ground/detect  (视觉模型返回元素像素框)
//   ocr-screenshot    → vision_long_screenshot_ocr (抄录图像文字)
//   crop              → vision_crop           (本地裁剪/放大)
//   dominant-colors   → vision_dominant_colors(主色/候选打分)
//   extract-foreground→ vision_extract_foreground (抠前景透明 PNG)
//
// 底层: 视觉模型走 VisionHttp (OpenAI 兼容 image_url), 本地图像处理走 VisionImage
// (stb_image)。流程编排由 assets/agent/skills 下的 SKILL.md 资产负责, 这里只出工具。
// ============================================================================

#include <vector>

#include "avox_agent/adapter/ProviderCatalog.hpp"
#include "avox_agent/compose/AgentHost.hpp"

namespace avox {

class AgentHost;

// conversationImageInput: 会话主模型是否支持图片输入 (ProviderCatalog imageInput)。
// true 时 see-image 的 description 把"通用看图"引导到 read_image, 把 OCR/对比/locate
// 等专项任务留给自己; false 时 see-image 是唯一看图入口, 维持原描述。
ToolDefinition makeSeeImageTool(bool conversationImageInput);
ToolDefinition makeLocateTool();
ToolDefinition makeOcrScreenshotTool(bool conversationImageInput);
ToolDefinition makePixelDiffTool();
ToolDefinition makeCropTool();
ToolDefinition makeDominantColorsTool();
ToolDefinition makeExtractForegroundTool();

// 统一注册到 host。catalog 由装配期 (composeDiagnosticAgent) 传入, 供应商彻底统一走
// ProviderCatalog (图像理解取 catalog.inputProviders(), 文生图取 catalog.genProviders())。
// conversationImageInput: 同上, 影响 see-image / ocr-screenshot 的 description 文案路由。
// 返回每个工具注册的撤销器 (单个失败由调用方记录, 尽量装完)。
std::vector<Disposer> registerVisionTools(AgentHost& host, const ProviderCatalog& catalog,
                                          bool conversationImageInput);

}