#include "VLayer.hpp"

namespace avox {

VLayer::VLayer(int32_t inSize, int32_t outSize) {
  inCount = inSize;
  outCount = outSize;
}

VLayer::~VLayer() {}

void VLayer::attach() {
  inFormats.resize(inCount);
  outFormats.resize(outCount);
  // 默认imagetype
  for (auto& format : inFormats) {
    format.imageType = ImageType::rgba8;
  }
  for (auto& format : outFormats) {
    format.imageType = ImageType::rgba8;
  }
  onInit();
}

void VLayer::detach() { onUnInit(); }

void VLayer::initBuffer() { onInitBuffer(); }

void VLayer::resetGraph() {
  if (pipeGraph) {
    pipeGraph->reset();
  }
}

}
