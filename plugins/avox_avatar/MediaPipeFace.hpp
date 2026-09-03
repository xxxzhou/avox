#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "avox/avatar/VideoFace.hpp"
#include "avox/module/Ringbuffer.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/vision/OnnxModel.hpp"
#include "avox/vision/OnnxModelUser.hpp"

namespace avox {

class IONNXSession;

// 单帧 RGBA8 (feed 深拷贝 -> worker onRunTask 取出推理)
struct MediaPipeChunk {
  std::vector<uint8_t> rgba;  // width*height*4 紧凑 rgba8
  int32_t width = 0;
  int32_t height = 0;
  int64_t pts = 0;
};

// BlazeFace 检测结果 (已映射到输入帧像素坐标)
struct FaceDetection {
  float score = 0;
  float cx = 0, cy = 0, w = 0, h = 0;  // 框中心/宽高 (像素)
  float kps[6][2] = {};                // 6 关键点 (像素): [0]=右眼 [1]=左眼 ...
};

// mediapipe 后端: RGBA 帧 -> ARKit52 blendshape + 478 landmarks (视频驱动 avatar)。
// 三明治继承: VideoFace(对外接口+Observer 分发) + RunTask(worker 推理) + OnnxModelUser(共享 session)。
// 数据流: feed(渲染线程) 深拷 rgba8 入队 -> onRunTask 取帧跑 5 步管线
//   (1 BlazeFace 检测 -> 2 眼角对齐 warp -> 3 landmarker 478 点 -> 4 blendshape 52 ->
//    5 landmark 经 M 反投回输入帧坐标) -> dispatch onFaceBlendshape + onFaceLandmarks。
// 数学严格移植自 tmp/face_pipeline.py (Python 已对照官方 mediapipe 校准: jawOpen MAE 0.016,
// 对齐对所有侧倾角 0-46° 鲁棒)。换后端只换本插件内部, IVideoFace 不变。
class MediaPipeFace : public VideoFace,
                      public RunTask,
                      public OnnxModelUser {
 public:
  MediaPipeFace();
  ~MediaPipeFace() override;

  // ========== VideoFace 接口 ==========
  void start() override;
  void feed(IImageBuffer* img, int64_t pts) override;
  void stop() override;
  bool loading() override;

 protected:
  // RunTask - 推理主循环 (initEngine -> 循环取帧推理)
  void onRunTask() override;

 private:
  bool initEngine();
  void processFrame(const MediaPipeChunk& chunk);
  // 5 步管线 (严格对应 face_pipeline.py); 返回 false = 未检出脸
  bool detectFace(const cv::Mat& rgb, FaceDetection& out);
  // warp + 仿射 M (dst->src, 须配 WARP_INVERSE_MAP); M 写入成员 lastM 供 back-project
  void alignWarp(const cv::Mat& rgb, const FaceDetection& det, cv::Mat& warp);

  RingBuffer<MediaPipeChunk> chunkQueue;
  std::mutex mutex;
  // 借用 (OnnxSessionCache/OnnxModelUser), 不 delete
  IONNXSession* detSession = nullptr;
  IONNXSession* lmSession = nullptr;
  IONNXSession* bsSession = nullptr;
  std::string detInName;
  std::vector<std::string> detOutNames;
  std::string lmInName;
  std::vector<std::string> lmOutNames;
  std::string bsInName;
  std::vector<std::string> bsOutNames;
  std::vector<float> anchors;       // [896*4] cx,cy,w,h 归一化 (initEngine 生成一次)
  // 预处理/推理复用缓冲 (避免每帧分配; onRunTask 单线程访问, 无需锁)
  std::vector<float> detInput;      // [128*128*3] RGB norm[-1,1]
  std::vector<float> lmInput;       // [256*256*3] RGB norm[-1,1]
  std::vector<float> bsInput;       // [146*2]
  std::vector<float> lm478;         // [478*3] warp 空间 landmark (landmarker 直出)
  std::vector<float> imgLandmarks;  // [478*2] 图像空间 (反投后, 供 onFaceLandmarks)
  std::vector<float> bs52;          // [52]
  cv::Mat lastM;                    // 最近一次 align warp 的 M (dst->src), back-project 用
  bool bInited = false;
};

}
