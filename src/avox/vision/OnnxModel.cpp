#include "OnnxModel.hpp"

#include "avox/Avox.hpp"  // getModelFilePath

namespace avox {

std::string onnxModelPath(OnnxModel m) {
  switch (m) {
    case OnnxModel::OcrDet:
      return getModelFilePath("ocr/ch_PP-OCRv6_det.onnx");
    case OnnxModel::OcrRec:
      return getModelFilePath("ocr/ch_PP-OCRv6_rec.onnx");
    case OnnxModel::YoloSegMini:
      return getModelFilePath("inpaint/yolo26n-seg_watermark.onnx");
    case OnnxModel::YoloSegBase:
      return getModelFilePath("inpaint/yolo26m-seg_watermark.onnx");
    case OnnxModel::YoloSegHigh:
      return getModelFilePath("inpaint/yolo26x-seg_watermark.onnx");
    case OnnxModel::Lama:
      return getModelFilePath("inpaint/lama_base.onnx");
    case OnnxModel::Aotgan:
      return getModelFilePath("inpaint/aotgan-onnx-float/aotgan.onnx");
    case OnnxModel::TransEncInt8:
      return getModelFilePath("translation/opus-mt-ja-zh") + "/encoder_model_int8.onnx";
    case OnnxModel::TransEnc:
      return getModelFilePath("translation/opus-mt-ja-zh") + "/encoder_model.onnx";
    case OnnxModel::TransDecInt8:
      return getModelFilePath("translation/opus-mt-ja-zh") + "/decoder_model_int8.onnx";
    case OnnxModel::TransDec:
      return getModelFilePath("translation/opus-mt-ja-zh") + "/decoder_model.onnx";
    case OnnxModel::RealESRGanX4V3:
      return getModelFilePath("quality/realesrgan-general-x4v3.onnx");
    case OnnxModel::RealESRGanX4V3Int8:
      return getModelFilePath("quality/realesrgan-general-x4v3_int8.onnx");
    case OnnxModel::Wav2ArkitCpu:
      // 权重在 wav2arkit_cpu.onnx.data (external-data, ORT 同目录自动加载); 两文件需一起部署
      return getModelFilePath("avatar/wav2arkit_cpu.onnx");
    case OnnxModel::MediaPipeFaceDetector:
      // BlazeFace 检测器 (takoyakisoft 导出); regressors[1,896,16]+classificators[1,896,1]
      // 解码非标准 exp: 全 16 维线性 /128 (经验反推); reverseOutputOrder box=[xc,yc,w,h]
      return getModelFilePath("avatar/face_detector.onnx");
    case OnnxModel::MediaPipeFaceLandmarker:
      // Google 官方 .task 经 takoyakisoft VisionOnnxExporter 导出 (非 yakhyo 移植版)
      // input_12[B,256,256,3] NHWC -> Identity[B,1,1,1434](=478x3)+presence[1]+scalar
      return getModelFilePath("avatar/face_landmarker.onnx");
    case OnnxModel::MediaPipeBlendshape:
      // Google 官方 blendshape 头 (非 py-feat 近似), batch 固定 1 须逐帧单跑
      // serving_default_input_points:0[1,146,2] -> StatefulPartitionedCall:0[52] 已sigmoid
      return getModelFilePath("avatar/face_blendshape.onnx");
    case OnnxModel::MediaPipePoseDetector:
      // BlazePose 检测器 (takoyakisoft 导出): input_1:0[1,3,224,224] NCHW 224x224
      // -> Identity[1,2254,12](box4+8kp) + Identity_1[1,2254,1] 分数
      return getModelFilePath("avatar/pose_detector.onnx");
    case OnnxModel::MediaPipePoseLandmarker:
      // 全身姿态 33 点 (takoyakisoft 导出): input_1[1,256,256,3] NHWC 256x256
      // -> Identity[1,195](33lmx3+aux) + presence[1,1] + seg/feature/world
      return getModelFilePath("avatar/pose_landmark.onnx");
  }
  return {};
}

}
