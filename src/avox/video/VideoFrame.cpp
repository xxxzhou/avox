#include "VideoFrame.hpp"

namespace avox {

VideoFrame::VideoFrame() {}

VideoFrame::~VideoFrame() {}

void VideoFrame::release() {
  if (!buffer) {
    return;
  }
  buffer->release();
}

void copyBufHost(VideoFramePtr& frame, const YUVFrame& curframe) {
  // 槽内帧可能正被编码线程读取(dequeue 后锁外处理), 独占才原地复用;
  // 边读边写会让画面内容和 pts 串到别的帧上(画面来回跳)
  if (!frame || frame.use_count() > 1) {
    frame = std::make_shared<VideoFrame>();
  }
  // 硬解切软解
  if (!frame->buffer || frame->buffer->getBufferType() != VBufferType::cpu) {
    frame->buffer = std::make_shared<SwVideoBuffer>();
  }
  std::shared_ptr<SwVideoBuffer> hostBuffer =
      std::static_pointer_cast<SwVideoBuffer>(frame->buffer);
  // 放入队列,需要复制curframe数据
  hostBuffer->form(curframe, true);
  frame->dts = curframe.dts;
  frame->pts = curframe.pts;
}

void copyBufGpu(VideoFramePtr& frame, const GpuFrame& gpuFrame) {
  // 同上: 消费线程持有期间不能原地换帧
  if (!frame || frame.use_count() > 1) {
    frame = std::make_shared<VideoFrame>();
  }
  // 软解切硬解
  if (!frame->buffer || frame->buffer->getBufferType() == VBufferType::cpu) {
    frame->buffer = std::make_shared<HwVideoBuffer>();
  }
  std::shared_ptr<HwVideoBuffer> hwBuffer =
      std::static_pointer_cast<HwVideoBuffer>(frame->buffer);
  hwBuffer->setGPUFrame(gpuFrame);
  frame->dts = gpuFrame.dts;
  frame->pts = gpuFrame.pts;
}

void copyBufRgba(VideoFramePtr& frame, const RgbaFrameRef& ref) {
  // 每次新分配: 环槽位复用同一 buffer 会在队列满反压时与仍在消费上帧的
  // 编码线程写同一对象(dequeue 只拷 shared_ptr), 离线帧便宜, 不做复用优化
  frame = std::make_shared<VideoFrame>();
  frame->buffer = std::make_shared<SwVideoBuffer>();
  std::shared_ptr<SwVideoBuffer> hostBuffer =
      std::static_pointer_cast<SwVideoBuffer>(frame->buffer);
  hostBuffer->copyFrom(ref.buf, true);
  hostBuffer->setYuvType(YuvType::other);
  frame->pts = ref.pts;
  frame->dts = ref.dts;
}

}