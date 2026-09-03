#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "avox/avatar/BodyImpl.hpp"
#include "avox/module/Ringbuffer.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/vision/OnnxModel.hpp"
#include "avox/vision/OnnxModelUser.hpp"

namespace avox {

class IONNXSession;

// 单帧 RGBA8 (feed 深拷贝 -> worker onRunTask 取出推理)
struct BodyChunk {
  std::vector<uint8_t> rgba;  // width*height*4 紧凑 rgba8
  int32_t width = 0;
  int32_t height = 0;
  int64_t pts = 0;
};

// BlazePose 检测结果 (已映射到输入帧像素坐标)
struct BodyDetection {
  float score = 0;
  float cx = 0, cy = 0, w = 0, h = 0;  // 框中心/宽高 (像素)
  // 4 关键点 (像素, 供旋转包围盒对齐): [0]=髋部中心 [1]=全身编码点 [2]=肩部中心 [3]=上半身编码点
  float kps[4][2] = {};
};

// mediapipe 后端: RGBA 帧 -> MediaPipe Pose 33 点身体 landmark (身体/手势驱动 avatar)。
// 三明治继承: BodyImpl(对外接口+Observer 分发) + RunTask(worker 推理) + OnnxModelUser(共享 session)。
// 数据流: feed(渲染线程) 深拷 rgba8 入队 -> onRunTask 取帧跑 3 步管线
//   (1 BlazePose 检测 2254 锚 -> 2 人体旋转包围盒对齐 warp 到 256 -> 3 pose_landmarker 33 点)
//   -> dispatch onBodyLandmarks。锚框解码对齐 MediaPipe SsdAnchorsCalculator (strides=[8,16,32,32,32])。
class MediaPipeBody : public BodyImpl,
                      public RunTask,
                      public OnnxModelUser {
 public:
  MediaPipeBody();
  ~MediaPipeBody() override;

  // ========== BodyImpl 接口 ==========
  void start() override;
  void feed(IImageBuffer* img, int64_t pts) override;
  void stop() override;
  bool loading() override;

 protected:
  // RunTask - 推理主循环 (initEngine -> 循环取帧推理)
  void onRunTask() override;

 private:
  bool initEngine();
  void processFrame(const BodyChunk& chunk);
  // 1. BlazePose 检测 (2254 锚); 返回 false = 未检出人
  bool detectBody(const cv::Mat& rgb, BodyDetection& out);
  // 2. 人体旋转包围盒对齐 warp 到 256; M 写入成员 lastM 供 landmark 反投回输入帧
  void alignBodyWarp(const cv::Mat& rgb, const BodyDetection& det, cv::Mat& warp);

  RingBuffer<BodyChunk> chunkQueue;
  std::mutex mutex;
  // 借用 (OnnxSessionCache/OnnxModelUser), 不 delete
  IONNXSession* detSession = nullptr;
  IONNXSession* lmSession = nullptr;
  std::string detInName;
  std::vector<std::string> detOutNames;
  std::string lmInName;
  std::vector<std::string> lmOutNames;
  std::vector<float> anchors;       // [2254*4] cx,cy,w,h 归一化 (initEngine 生成一次)
  // 预处理/推理复用缓冲 (onRunTask 单线程访问, 无需锁)
  std::vector<float> detInput;      // [3*224*224] NCHW RGB norm[-1,1]
  std::vector<float> lmInput;       // [256*256*3] NHWC RGB norm[-1,1]
  std::vector<float> body33;        // [33*4] 图像空间 landmark xyz + conf(spatial softmax 后热图峰值) (反投后, 供 onBodyLandmarks)
  std::vector<float> hmBuf;         // [64*64*33] argmax 后的热图 (每通道 64x64 argmax 索引), 复用
  cv::Mat lastM;                    // 最近一次 align body warp 的 M (dst->src), back-project 用
  bool bInited = false;
};

}
