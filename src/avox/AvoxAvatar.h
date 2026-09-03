#pragma once
#include <stdint.h>

#include "AvoxBuffer.h"
#include "AvoxBase.h"

namespace avox {

// 前置声明: feed 取 RGBA8 IImageBuffer (来自 ISurfaceRender::enableImage 回读);
// 完整定义在 AvoxVideo.h, 调用方(已持有 player/surface)本就 include 它, 此处不拉入
class IImageBuffer;

// ============== 视频→面部 blendshape + landmarks (视频驱动 avatar 域) ==============
// avatar 域(视频驱动): RGBA 帧 进 (feed, 来自 SurfaceRender::enableImage 回读)
//   → ARKit52 blendshape 出 (驱动虚拟人) + 478 landmarks 出 (图像坐标, 供几何渲染可视化)。
// 与 AvoxAudio.h 的 IAudioFace 对称 (音频驱动), 实现由 avox_avatar 插件 (MediaPipeFace) 提供。
// 后续身体/手驱动另立接口, 留待扩展。

// 输出描述 (onFaceDesc 回调一次, 随后开始 onFaceBlendshape/onFaceLandmarks)
struct VideoFaceDesc {
  int32_t fps = 30;              // 输出帧率 (随输入帧)
  int32_t blendshapeCount = 52;  // ARKit52
  int32_t landmarkCount = 478;   // MediaPipe face mesh 点数
};

// ============== 视频→身体姿态 33 点 (身体/手势驱动 avatar 域) ==============
// 身体(视频驱动): RGBA 帧 进 (feed) → MediaPipe Pose 33 点身体 landmark 出
//   (图像坐标 x,y,z, 前 33 点), 供 retargeting 驱动 avatar 全身骨骼。实现由 avox_avatar
//   插件 (MediaPipeBody) 提供, 独立于 IVideoFace (脸)。后续手驱动另立接口。

// 身体姿态输出描述 (onBodyDesc 回调一次, 随后开始 onBodyLandmarks)
struct BodyDesc {
  int32_t fps = 30;            // 输出帧率 (随输入帧)
  int32_t landmarkCount = 33;  // MediaPipe Pose 身体关键点数
};

// 视频→身体 回调接口
class IBodyOb {
 public:
  virtual ~IBodyOb() = default;
  // 模型就绪后回调一次输出描述
  virtual void onBodyDesc(const BodyDesc& desc) {}
  // 单帧 33 点身体 landmark 就绪: raw = count×4 float 交错 (x,y,z,conf),
  // 像素坐标在 feed 帧空间 (imgW×imgH), z 为相对深度 (髋部为原点, 与 x/y 同尺度),
  // conf 为每点 spatial softmax 后热图峰值 (0~1, 低=遮挡/模型外推, 供低可信不驱动)。
  // raw 仅回调内有效 (bRef), 观察方需即时拷贝; 未检出人时 count=0 (仍回调, 清空驱动)。
  virtual void onBodyLandmarks(const AvoxData& raw, int32_t count,
                               int32_t imgW, int32_t imgH, int64_t pts) {}
  // 推理失败
  virtual void onBodyError(const char* err) {}
};

// 视频→身体 接口 (对外导出)
// 方向: RGBA 帧 进 (feed) → 33 点身体 landmark 出 (经 IBodyOb)
class IBody {
 public:
  virtual ~IBody() = default;
  // 开始推理任务 (内部按需加载/复用模型)
  virtual void start() = 0;
  // 喂一帧 RGBA8 (IImageBuffer, 来自 enableImage 回读); body landmarks 经回调出
  virtual void feed(IImageBuffer* img, int64_t pts) = 0;
  // 停止任务 (flush 剩余 + join worker, 模型常驻)
  virtual void stop() = 0;
  virtual bool loading() = 0;
};

extern "C" {
// 创建视频→身体 推理器 (后端未注册返回 nullptr)
AVOX_EXPORT IBody* createBody(const char* type);
AVOX_EXPORT void addBodyOb(IBody* body, IBodyOb* ob);
AVOX_EXPORT void removeBodyOb(IBody* body, IBodyOb* ob);
}

// 后端类型 (创建实例用, 内部映射工厂表字符串 key)
enum class VideoFaceType { none, mediapipe };

// 视频→面部 回调接口
class IVideoFaceOb {
 public:
  virtual ~IVideoFaceOb() = default;
  // 模型就绪后回调一次输出描述
  virtual void onFaceDesc(const VideoFaceDesc& desc) {}
  // 单帧 blendshape 就绪: raw52 = blendshapeCount×float (值域[0,1]), raw 仅回调内有效 (bRef);
  // pts = 该帧时间戳(ms); final=true 表示本次 feed 触发推理的最后帧; 无脸时仍回调 (零值/hold)
  virtual void onFaceBlendshape(const AvoxData& raw52, int64_t pts, bool final) {}
  // 单帧 landmarks 就绪: rawPts = count×2 float 交错 (x,y), 像素坐标在 feed 帧空间 (imgW×imgH),
  // 供调用方按 imgW/imgH 归一化后叠加渲染。raw 仅回调内有效 (bRef), 观察方需即时拷贝;
  // 未检出脸时 count=0 (仍回调, 调用方据此清空叠加)
  virtual void onFaceLandmarks(const AvoxData& rawPts, int32_t count,
                               int32_t imgW, int32_t imgH, int64_t pts) {}
  // 推理失败
  virtual void onFaceError(const char* err) {}
};

// 视频→面部 接口 (对外导出)
// 方向: RGBA 帧 进 (feed) → ARKit52 blendshape + 478 landmarks 出 (经 IVideoFaceOb)
class IVideoFace {
 public:
  virtual ~IVideoFace() = default;
  // 开始推理任务 (内部按需加载/复用模型)
  virtual void start() = 0;
  // 喂一帧 RGBA8 (IImageBuffer, 来自 enableImage 回读); blendshape/landmarks 经回调出
  virtual void feed(IImageBuffer* img, int64_t pts) = 0;
  // 停止任务 (flush 剩余 + join worker, 模型常驻)
  virtual void stop() = 0;
  virtual bool loading() = 0;
  virtual void setModelLevel(ModelLevel level) = 0;
};

extern "C" {
// 创建视频→面部 推理器 (后端未注册/none 返回 nullptr)
AVOX_EXPORT IVideoFace* createVideoFace(VideoFaceType type);
AVOX_EXPORT void addVideoFaceOb(IVideoFace* face, IVideoFaceOb* ob);
AVOX_EXPORT void removeVideoFaceOb(IVideoFace* face, IVideoFaceOb* ob);
}

// ============== ARKit52 canonical 顺序 (音频/视频两路统一) ==============
// onFaceBlendshape 下游唯一顺序 = MediaPipe blendshape 分类表: index0=_neutral 占位 (恒 0, 不驱动);
// wav2arkit 原生序与本表 51 个实名同序 (browDownLeft..noseSneerRight, 尾格 tongueOut 本表无),
// 插件输出已整体 +1 对齐。修改顺序须同步: plugins/avox_avatar (Wav2ArkitFace 重排 /
// MediaPipeFace 原生同序) 与 platform/godot/tools/src/avatar/arkit52.gd (消费端名表)。
extern "C" {
// 52 个 blendshape 名, 逗号分隔 (静态存储, 无需释放)
AVOX_EXPORT const char* getArkit52NamesCsv();
}

}
