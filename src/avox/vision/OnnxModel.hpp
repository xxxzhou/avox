#pragma once

#include <string>

#include "avox/AvoxDef.h"

namespace avox {

// 经 IONNXSession 加载的 ONNX 模型身份 (OnnxSessionCache 的 key 之一).
// level / int8 变体拆成不同 enum 值, 调用方按配置选 (它本就有这 switch/fallback).
// 路径经 onnxModelPath() 集中构建, 调用方不再自己拼路径.
// 注: Trans* 值仅非 Android 可用 —— Android 下 translation 走 cache 拷贝目录(运行期路径),
// 应改用 OnnxSessionCache::acquire(string)/OnnxModelUser::session(string).
enum class OnnxModel {
  OcrDet,        // PP-OCRv6 det
  OcrRec,        // PP-OCRv6 rec
  YoloSegMini,   // inpaint YOLO-Seg mini
  YoloSegBase,   // inpaint YOLO-Seg base
  YoloSegHigh,   // inpaint YOLO-Seg high
  Lama,          // inpaint LaMa
  Aotgan,        // inpaint AOT-GAN
  TransEncInt8,  // translation encoder int8 (非 Android)
  TransEnc,      // translation encoder fp32 (非 Android)
  TransDecInt8,  // translation decoder int8 (非 Android)
  TransDec,      // translation decoder fp32 (非 Android)
  RealESRGanX4V3,      // Real-ESRGAN general-x4v3 画质增强 FP32
  RealESRGanX4V3Int8,  // 同上 INT8 (conv-only QDQ 量化, PReLU 保 FP32; Intel CPU VNNI 加速)
  Wav2ArkitCpu,        // wav2arkit 音频->ARKit52 blendshape (虚拟人口型; 16kHz PCM->[1,frames,52])
  MediaPipeFaceDetector,    // MediaPipe 视频驱动 BlazeFace 检测: 任意帧 -> 896 候选 (takoyakisoft 导出; input[1,128,128,3] NHWC RGB norm[-1,1] -> regressors[1,896,16](box4+6kp×2 线性/128)+classificators[1,896,1]; reverseOutputOrder box=[xc,yc,w,h])
  MediaPipeFaceLandmarker,  // MediaPipe 视频驱动: 256x256 人脸裁剪 -> 478 landmarks (Google 官方 .task 经 takoyakisoft VisionOnnxExporter 导出; input_12[B,256,256,3] NHWC -> Identity[B,1,1,1434]=478x3 landmark-major + presence + scalar)
  MediaPipeBlendshape,      // MediaPipe 视频驱动: 146 子集 landmarks(x,y) -> 52 ARKit blendshape (Google 官方 .task blendshape 头; serving_default_input_points:0[1,146,2] batch固定1 -> StatefulPartitionedCall:0[52] 已sigmoid)
  MediaPipePoseDetector,    // MediaPipe 全身姿态检测器 BlazePose (takoyakisoft 导出): input_1:0[1,3,224,224] NCHW -> Identity[1,2254,12](box4+8kp)+Identity_1[1,2254,1] 分数
  MediaPipePoseLandmarker,  // MediaPipe 全身姿态 33 点 (takoyakisoft 导出): input_1[1,256,256,3] NHWC -> Identity[1,195](33lmx3+aux)+presence[1,1]+seg/feature/world
};

// enum -> 模型路径. 沿用各消费者原有解析 (OCR asset 相对; inpaint/translation 经 getModelFilePath),
// 零解析行为变更, 仅集中路径字面量. 未知值返回空串.
AVOX_EXPORT std::string onnxModelPath(OnnxModel m);

}
