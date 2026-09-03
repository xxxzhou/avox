#pragma once

#include "avox/AvoxVision.h"  // ITemplateMatcher, MatchResult
#include "avox/vision/OcrHelper.hpp"  // LocateResult

#include <string>
#include <vector>

namespace avox {

// 全量模板匹配: 在 scene 中匹配 template, 返回所有命中结果 (位置+置信度)
// 结果按排序规则排列 (默认 horizontal); 插件不可用返回空 vector
std::vector<MatchResult> matchAll(IImageBuffer* scene, IImageBuffer* tmpl,
                                  double threshold = 0.7);

// 图像模板匹配: 在 buffer 内匹配 tBuffer (取首个命中) → imgCenter。纯视觉,
// 不转坐标、不动作。 命中填 out(found/score/imgCenter) 并返回 true;
// 未命中/插件不可用返回 false。
bool locateImage(IImageBuffer* buffer, IImageBuffer* tBuffer, double threshold,
                 LocateResult* out);

// 格式化模板匹配结果为可读文本, 供提交给不支持图片的大模型
// 格式: 每行一条 "(x=10,y=20,w=60,h=30, score=0.95, tpl=0)"
std::string formatMatchResult(const std::vector<MatchResult>& items);

}
