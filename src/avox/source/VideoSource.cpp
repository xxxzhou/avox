#include "VideoSource.hpp"

#include "../module/LogHelper.hpp"

namespace avox {

void VideoSource::onFrame(const YUVFrame& frame) {
  checkSizeChanged(frame.format);
  dispatch(&IVideoSourceOb::onVideoFrame, frame);
}

void VideoSource::onFrame(const GpuFrame& frame) {
  // log(LogLevel::info, "VideoSource:", frame.pts);
  checkSizeChanged(frame.format);
  dispatch(&IVideoSourceOb::onGpuFrame, frame);
}

bool VideoSource::checkSizeChanged(const YUVFormat& format) {
  if (!bFirstFrame) {
    curDesc.width = format.width;
    curDesc.height = format.height;
    curDesc.type = format.type;
    dispatch(&IVideoSourceOb::onVideoDesc, curDesc);
    bFirstFrame = true;
    return false;
  } else {
    if (curDesc.width != format.width || curDesc.height != format.height ||
        curDesc.type != format.type) {
      curDesc.width = format.width;
      curDesc.height = format.height;
      curDesc.type = format.type;
      dispatch(&IVideoSourceOb::onVideoDesc, curDesc);
      return true;
    }
    return false;
  }
}

void VideoSource::setVideoDesc(int32_t width, int32_t height, int32_t fps) {
  if (descList.empty()) return;
  VideoDesc bestDesc = descList[0];
  int64_t bestScore = INT64_MAX;

  int32_t tMax = std::max(width, height);
  int32_t tMin = std::min(width, height);
  float tAspect = (float)tMax / tMin;

  for (const auto& desc : descList) {
    int64_t score = 0;
    int32_t dMax = std::max(desc.width, desc.height);
    int32_t dMin = std::min(desc.width, desc.height);
    float dAspect = (float)dMax / dMin;
    // 1. 比例差异 (最重要)
    score += static_cast<int64_t>(abs(tAspect - dAspect) * 50000);
    // 2. 分辨率绝对差距 (长边对长边)
    score += abs(tMax - dMax) * 20;
    // 3. 宁大勿小惩罚
    if (dMax < tMax) score += 15000;
    // 4. 格式奖励 (NV12优先)
    if (desc.type == YuvType::nv12) score -= 1000;
    // 5. FPS 匹配
    if (fps > 0) {
      score += abs(desc.fps - fps) * 100;
    }
    if (score < bestScore) {
      bestScore = score;
      bestDesc = desc;
    }
  }
  curDesc = bestDesc;
  LOGFLF(LogLevel::info, "selected video format: ", bestDesc,
         " score: ", bestScore);
}

}
