#include "VkFSREASULayer.hpp"

#include "../layer/VkPipeGraph.hpp"
#include "avox/AvoxMath.h"

namespace avox {

VkFSREASULayer::VkFSREASULayer() {
  glslPath = "glsl/fsr_easu.comp.spv";
  // UBO: 4 x vec4 (con0, con1, con2, con3)
  setUBOSize(sizeof(float) * 16);
}

VkFSREASULayer::~VkFSREASULayer() {}

bool VkFSREASULayer::getSampled(int32_t inIndex) {
  // EASU uses sampler2D for input (not storage image)
  return (inIndex == 0);
}

bool VkFSREASULayer::sampledNearest(int32_t inIndex) {
  // Always linear sampling for EASU
  return false;
}

void VkFSREASULayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba16f;
  outFormats[0].imageType = ImageType::rgba16f;
  VkLayer::onInitGraph();
}

void VkFSREASULayer::onInitLayer() {
  // Output size = input size * scale
  outFormats[0].width = inFormats[0].width * scale;
  outFormats[0].height = inFormats[0].height * scale;

  // Compute FsrEasuCon constants on CPU
  float srcW = float(inFormats[0].width);
  float srcH = float(inFormats[0].height);
  float dstW = float(outFormats[0].width);
  float dstH = float(outFormats[0].height);

  // con0: output integer position to pixel position in viewport
  float con0[4];
  con0[0] = srcW / dstW;
  con0[1] = srcH / dstH;
  con0[2] = srcW / dstW * 0.5f - 0.5f;
  con0[3] = srcH / dstH * 0.5f - 0.5f;

  // con1: viewport pixel position to normalized image space
  float con1[4];
  con1[0] = 1.0f / srcW;
  con1[1] = 1.0f / srcH;
  con1[2] = 1.0f / srcW;
  con1[3] = -1.0f / srcH;

  // con2: offsets from upper-left of 'F' for gather positions
  float con2[4];
  con2[0] = -1.0f / srcW;
  con2[1] = 2.0f / srcH;
  con2[2] = 1.0f / srcW;
  con2[3] = 2.0f / srcH;

  // con3: additional offsets
  float con3[4];
  con3[0] = 0.0f;
  con3[1] = 4.0f / srcH;
  con3[2] = 0.0f;
  con3[3] = 0.0f;

  // Pack into UBO: 4 x vec4 = 16 floats
  float uboData[16];
  memcpy(uboData, con0, 16);
  memcpy(uboData + 4, con1, 16);
  memcpy(uboData + 8, con2, 16);
  memcpy(uboData + 12, con3, 16);
  updateUBO(uboData);

  // Dispatch on output resolution
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}

}
