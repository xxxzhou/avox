#pragma once

#include "avox/AvoxVision.h"  // ITextRecognizer, OcrResult
#include "avox/AvoxMath.h"    // vec2i

#include <string>
#include <vector>

namespace avox {

// 在 buffer 内定位的结果 (纯视觉, 不做坐标转换、不触发动作)。
struct LocateResult {
  bool found = false;
  double score = 0;
  vec2i imgCenter{0, 0};  // 命中区域在 buffer 内的中心坐标
  vec2i screen{0, 0};     // 命中处屏幕坐标 (info.left/top + imgCenter; findText/findImage 填)
  std::string text;       // text 模式命中文本
};

// OCR 全量识别结果 (内部用, 含文字串; 不暴露到跨 DLL 接口)
struct OcrItem {
  int32_t x = 0, y = 0, w = 0, h = 0;
  double score = 0.0;
  std::string text;
};

// 全量 OCR 识别: 对 buffer 做完整 OCR, 返回所有识别结果 (含文字+位置+置信度)
// 结果按阅读顺序排列 (y 升序, 同 y 按 x 升序); 插件不可用/模型加载失败返回空 vector
std::vector<OcrItem> recognizeAll(IImageBuffer* buffer, double threshold = 0.3);

// OCR 文字匹配: 在 buffer 内找含 text 的文本 (取首个命中) → imgCenter。纯视觉,
// 不转坐标、不动作。 命中填 out(found/score/imgCenter/text) 并返回 true;
// 未命中/插件不可用返回 false。
bool locateText(IImageBuffer* buffer, const char* text, double threshold,
                LocateResult* out);

// 格式化 OCR 结果为可读文本, 供提交给不支持图片的大模型
// 格式: 每行一条 "文字 (x=10,y=20,w=60,h=30, score=0.95)", 按阅读顺序
std::string formatOcrResult(const std::vector<OcrItem>& items);

}
