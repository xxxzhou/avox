#include "VideoYuv.hpp"

namespace avox {

void VideoYuv::renderFrame(const GpuFrame& frame) {
  if (!vaildAndInitGraph(frame)) {
    return;
  }
  renderGpuFrame(frame);
}

}