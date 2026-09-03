#include "VkMotionBlurLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

using namespace std::placeholders;

namespace avox {

VkMotionBlurLayer::VkMotionBlurLayer(/* args */) {
  glslPath = "glsl/motionBlur.comp.spv";
  setUBOSize(sizeof(vec2f));
  // 需要用到inFormats[0]的长宽,只有等到onInitLayer才知道
  // transformParamet();
}

VkMotionBlurLayer::~VkMotionBlurLayer() {}

void VkMotionBlurLayer::transformParamet() {
  assert(inFormats[0].width > 0 && inFormats[0].height > 0);
  float aspectRatio = (float)inFormats[0].height / inFormats[0].width;
  vec2f offset = {};
  offset.x = paramet.blurSize * cos(paramet.blurAngle * M_PI / 180.0) *
             aspectRatio / inFormats[0].width;
  offset.y = paramet.blurSize * sin(paramet.blurAngle * M_PI / 180.0) /
             inFormats[0].height;
  updateUBO(&offset);
}

bool VkMotionBlurLayer::getSampled(int32_t inIndex) { return inIndex == 0; }

void VkMotionBlurLayer::onUpdateParamet() {
  if (inFormats.size() > 0 && inFormats[0].height > 0 &&
      inFormats[0].width > 0) {
    if (!(paramet == oldParamet)) {
      transformParamet();
      bParametChange = true;
    }
  }
}

void VkMotionBlurLayer::onInitLayer() {
  VkLayer::onInitLayer();
  transformParamet();
}

VkZoomBlurLayer::VkZoomBlurLayer(/* args */) {
  glslPath = "glsl/zoomBlur.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkZoomBlurLayer::~VkZoomBlurLayer() {}

bool VkZoomBlurLayer::getSampled(int32_t inIndex) { return inIndex == 0; }

// VkMotionDetectorLayer::VkMotionDetectorLayer(/* args */) {
//   glslPath = "glsl/motionDetector.comp.spv";
//   inCount = 2;

//   lowLayer = std::make_unique<VkLowPassLayer>();
//   avageLayer = std::make_unique<VkReduceLayer>(ReduceOperate::sum);
//   outLayer = std::make_unique<VkOutputLayer>();

//   paramet = 0.5f;
//   lowLayer->updateParamet(paramet);
//   outLayer->setObserver(this);
// }

// void VkMotionDetectorLayer::setObserver(IMotionDetectorObserver* observer) {
//   this->observer = observer;
// }

// void VkMotionDetectorLayer::onImageProcess(uint8_t* data,
//                                            const ImageFormat& format,
//                                            int32_t outIndex) {
//   if (observer) {
//     vec4f motion = {};
//     memcpy(&motion, data, sizeof(vec4f));
//     float size = inFormats[0].width * inFormats[0].height;
//     motion.z = motion.z / size;
//     motion.w = motion.w / size;
//     observer->onMotion(motion);
//   }
// }

// VkMotionDetectorLayer::~VkMotionDetectorLayer() {}

// void VkMotionDetectorLayer::onUpdateParamet() {
//   lowLayer->updateParamet(paramet);
// }

// void VkMotionDetectorLayer::onInitGroup() {
//   pipeGraph->addNode(lowLayer->getLayer());
//   pipeGraph->addNode(avageLayer)->addNode(outLayer->getLayer());
// }

// void VkMotionDetectorLayer::onInitNode() {
//   lowLayer->addLine(this, 0, 1);
//   this->addLine(avageLayer);
//   setStartNode(this, 0);
//   setStartNode(lowLayer);
// }

}