#include "../AvoxVision.h"

#include "../module/AvoxManager.hpp"

namespace avox {

// 经工厂表拿实例 (由 avox_ocr plugin 注册 "ppocr"); avox_ocr 未启用时 create 返回 nullptr 降级
ITextRecognizer* createTextRecognizer() {
  return AvoxManager::Get().textRecognizerHub.create("ppocr");
}

// 经工厂表拿实例 (由 avox_opencv plugin 注册 "opencv"); avox_opencv 未启用时返回 nullptr 降级
ITemplateMatcher* createTemplateMatcher() {
  return AvoxManager::Get().templateMatcherHub.create("opencv");
}

// 框中心 = 左上角 + 宽高一半 (供 Agent 点击)
vec2i getMatchCenter(const MatchResult& r) {
  return vec2i(r.x + r.w / 2, r.y + r.h / 2);
}

vec2i getOcrCenter(const OcrResult& r) {
  return vec2i(r.x + r.w / 2, r.y + r.h / 2);
}

// 经工厂表拿实例 (由 avox_opencv plugin 注册 "opencv"); avox_opencv 未启用时返回 nullptr 降级
IFeatureMatcher* createFeatureMatcher() {
  return AvoxManager::Get().featureMatcherHub.create("opencv");
}

IColorDetector* createColorDetector() {
  return AvoxManager::Get().colorDetectorHub.create("opencv");
}

IOrientationDetector* createOrientationDetector() {
  return AvoxManager::Get().orientationDetectorHub.create("opencv");
}

IMapMatcher* createMapMatcher() {
  return AvoxManager::Get().mapMatcherHub.create("opencv");
}

// 经工厂表拿实例 (由 avox_opencv plugin 注册 "opencv"); avox_opencv 未启用时返回 nullptr 降级
IMaskBuilder* createMaskBuilder() {
  return AvoxManager::Get().maskBuilderHub.create("opencv");
}

// 经工厂表拿实例 (由 avox_cv plugin 注册 "yolo"); avox_cv 未启用时返回 nullptr 降级
IYoloDetector* createYoloDetector() {
  return AvoxManager::Get().yoloDetectorHub.create("yolo");
}

vec2i getFeatureMatchCenter(const FeatureMatchResult& r) {
  return vec2i(r.x + r.w / 2, r.y + r.h / 2);
}

vec2i getColorRegionCenter(const ColorRegion& r) {
  return vec2i(r.x + r.w / 2, r.y + r.h / 2);
}

}
