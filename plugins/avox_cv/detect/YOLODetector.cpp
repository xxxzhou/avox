#include "YOLODetector.hpp"

#include "avox/module/LogHelper.hpp"

#include <algorithm>
#include <cmath>

namespace avox {

YOLODetector::YOLODetector() {}
YOLODetector::~YOLODetector() { close(); }

void YOLODetector::close() {
  // session 借用自 OnnxSessionCache(Shared), 对象不释放模型; 仅清自身指针与缓冲
  session = nullptr;
  preprocessed.clear();
}

bool YOLODetector::open(OnnxModel model, bool useGPU) {
  // 经基类 OnnxModelUser 从全局缓存取 (首次加载, 之后复用, 常驻不释放)
  session = OnnxModelUser::session(model, useGPU);
  if (!session) {
    LOGFLF(LogLevel::error, "[YOLODetector] ONNX session 加载失败");
    return false;
  }

  // 自动适配输入名称 (images / image / l_image 等)
  auto inputNames = session->getInputNames();
  for (const auto& name : inputNames) {
    imageInputName = name;
    break;  // 取第一个输入名
  }
  if (imageInputName.empty()) {
    imageInputName = "images";  // 兜底
  }

  // 获取输入尺寸
  auto shape = session->getInputShape(imageInputName);
  if (shape.size() >= 3) {
    inputSize = static_cast<int>(shape[2]);
  }

  // 检测是否为分割模型 (有两个输出)
  auto outputNames = session->getOutputNames();
  isSegModel = (outputNames.size() >= 2);

  // 从输出 shape 获取类别数
  // YOLO 检测输出格式: [1, 4+numClasses, numAnchors]
  // YOLO-Seg 输出格式: [1, 4+numClasses+32, numAnchors] + [1, 32, maskH, maskW]
  auto outputShape = session->getOutputShape("output0");
  if (outputShape.size() >= 2) {
    int channels = static_cast<int>(outputShape[1]);
    if (isSegModel) {
      // 分割模型: channels = 4 + numClasses + 32 (mask coeffs)
      numClasses = std::max(1, channels - 4 - 32);
    } else if (channels > 4) {
      numClasses = channels - 4;
    }
  }

  // 获取分割掩码尺寸
  if (isSegModel && outputNames.size() >= 2) {
    auto protoShape = session->getOutputShape(outputNames[1]);
    if (protoShape.size() >= 3) {
      maskSize = static_cast<int>(protoShape[2]);
    }
  }

  // 预分配预处理缓冲
  preprocessed.resize(inputSize * inputSize * 3);

  return true;
}

void YOLODetector::letterbox(const uint8_t* image, int width, int height,
                              float& scaleX, float& scaleY) {
  // 使用直接 resize (和训练时一致)
  // 计算缩放比例 (不保持纵横比，直接缩放到 inputSize x inputSize)
  scaleX = static_cast<float>(inputSize) / width;
  scaleY = static_cast<float>(inputSize) / height;

  // NCHW 格式: [3, H, W]
  size_t planeSize = static_cast<size_t>(inputSize) * inputSize;

  // 缩放并归一化 - 输出为 NCHW 格式 [C, H, W]
  float* rPlane = preprocessed.data();
  float* gPlane = preprocessed.data() + planeSize;
  float* bPlane = preprocessed.data() + 2 * planeSize;

  for (int y = 0; y < inputSize; y++) {
    for (int x = 0; x < inputSize; x++) {
      // 直接映射到原图坐标
      int srcX = static_cast<int>(x / scaleX);
      int srcY = static_cast<int>(y / scaleY);

      srcX = std::min(srcX, width - 1);
      srcY = std::min(srcY, height - 1);

      int srcIdx = (srcY * width + srcX) * 3;
      int dstIdx = y * inputSize + x;

      // RGB 顺序，归一化到 0-1，存储为 NCHW 格式
      rPlane[dstIdx] = image[srcIdx + 0] / 255.0f;  // R
      gPlane[dstIdx] = image[srcIdx + 1] / 255.0f;  // G
      bPlane[dstIdx] = image[srcIdx + 2] / 255.0f;  // B
    }
  }
}

std::vector<WatermarkBBox> YOLODetector::detect(const uint8_t* rgbImage,
                                                 int width, int height,
                                                 float confThreshold,
                                                 float nmsThreshold) {
  if (!ready()) {
    return {};
  }

  // 预处理
  float scaleX, scaleY;
  letterbox(rgbImage, width, height, scaleX, scaleY);

  // 推理
  std::vector<std::pair<std::string, const float*>> inputs = {
      {imageInputName, preprocessed.data()}
  };

  std::vector<std::vector<float>> outputs;
  if (!session->run(inputs, {"output0"}, outputs)) {
    return {};
  }

  // 后处理
  // YOLO 输出格式 (ONNX): [1, numAnchors, 4+numClasses]
  // 4 = bbox (cx, cy, w, h), numClasses = 类别数
  auto outputShape = session->getOutputShape("output0");
  const float* outputData = outputs[0].data();
  int numAnchors = static_cast<int>(outputShape[1]);
  int channels = static_cast<int>(outputShape[2]);

  return postprocess(outputData, numAnchors, channels, width, height,
                     scaleX, scaleY, confThreshold, nmsThreshold);
}

std::vector<WatermarkSeg> YOLODetector::detectWithMask(const uint8_t* rgbImage,
                                                        int width, int height,
                                                        float confThreshold,
                                                        float nmsThreshold) {
  if (!ready() || !isSegModel) {
    return {};
  }

  // 预处理
  float scaleX, scaleY;
  letterbox(rgbImage, width, height, scaleX, scaleY);

  // 推理
  std::vector<std::pair<std::string, const float*>> inputs = {
      {imageInputName, preprocessed.data()}
  };

  auto outputNames = session->getOutputNames();

  std::vector<std::vector<float>> outputs;
  if (!session->run(inputs, outputNames, outputs)) {
    return {};
  }

  // YOLO-Seg 输出 (ONNX): [1, numAnchors, 4+nc+32] - 检测 + mask coeffs
  // output1: [1, 32, maskH, maskW] - mask prototypes
  auto outputShape = session->getOutputShape(outputNames[0]);
  const float* detOutput = outputs[0].data();
  const float* protoOutput = (outputs.size() >= 2) ? outputs[1].data() : nullptr;

  int numAnchors = static_cast<int>(outputShape[1]);
  int channels = static_cast<int>(outputShape[2]);

  return postprocessSeg(detOutput, protoOutput, numAnchors, channels,
                        width, height, scaleX, scaleY,
                        confThreshold, nmsThreshold);
}

std::vector<WatermarkSeg> YOLODetector::postprocessSeg(
    const float* detOutput, const float* protoOutput,
    int numAnchors, int channels, int origWidth, int origHeight,
    float scaleX, float scaleY,
    float confThresh, float nmsThresh) {

  std::vector<WatermarkSeg> candidates;

  // ONNX 输出格式: [1, numAnchors, channels] -> [anchors, channels]
  // channels = 38: 0-3=xyxy(bbox), 4=obj_conf, 5=class_conf(单类为0), 6-37=mask_coeffs
  // 对于单类检测，直接使用 obj_conf 作为置信度
  for (int i = 0; i < numAnchors; i++) {
    // 获取置信度 (单类检测直接用 obj_conf)
    float conf = detOutput[i * channels + 4];

    // 过滤低置信度
    if (conf < confThresh) continue;

    // 获取边界框 (xyxy 格式: x1, y1, x2, y2) - 在模型输入空间 (640x640)
    float lx1 = detOutput[i * channels + 0];
    float ly1 = detOutput[i * channels + 1];
    float lx2 = detOutput[i * channels + 2];
    float ly2 = detOutput[i * channels + 3];

    // 映射回原图坐标 (直接 resize，分别用 scaleX 和 scaleY)
    float x1 = lx1 / scaleX;
    float y1 = ly1 / scaleY;
    float x2 = lx2 / scaleX;
    float y2 = ly2 / scaleY;

    // 计算宽高
    float w = x2 - x1;
    float h = y2 - y1;

    // 裁剪到图像范围
    x1 = std::max(0.0f, x1);
    y1 = std::max(0.0f, y1);
    w = std::min(w, origWidth - x1);
    h = std::min(h, origHeight - y1);

    // 创建分割结果
    WatermarkSeg seg;
    seg.bbox.x = x1;
    seg.bbox.y = y1;
    seg.bbox.width = w;
    seg.bbox.height = h;
    seg.bbox.confidence = conf;
    seg.bbox.classId = 0;  // 单类检测

    // 生成分割掩码
    if (protoOutput) {
      // 获取 mask 系数 (32 个)
      std::vector<float> maskCoeffs(32);
      int maskCoeffStart = 6;  // xyxy(4) + obj_conf(1) + class_conf(1) = 6
      for (int j = 0; j < 32; j++) {
        maskCoeffs[j] = detOutput[i * channels + maskCoeffStart + j];
      }

      // bbox在mask空间的坐标 (640空间 -> 160空间)
      float protoScale = static_cast<float>(maskSize) / inputSize;
      int mx1 = std::max(0, static_cast<int>(lx1 * protoScale));
      int my1 = std::max(0, static_cast<int>(ly1 * protoScale));
      int mx2 = std::min(maskSize, static_cast<int>(lx2 * protoScale));
      int my2 = std::min(maskSize, static_cast<int>(ly2 * protoScale));

      // 扩展一点边界
      int margin = 5;
      mx1 = std::max(0, mx1 - margin);
      my1 = std::max(0, my1 - margin);
      mx2 = std::min(maskSize, mx2 + margin);
      my2 = std::min(maskSize, my2 + margin);

      // 裁剪后的 mask 尺寸
      int cropW = std::max(1, mx2 - mx1);
      int cropH = std::max(1, my2 - my1);

      seg.maskWidth = cropW;
      seg.maskHeight = cropH;
      seg.mask.resize(cropW * cropH, 0);

      // 只计算bbox区域内的mask
      for (int my = my1; my < my2; my++) {
        for (int mx = mx1; mx < mx2; mx++) {
          // 矩阵乘法: sum(maskCoeffs[j] * proto[j, my, mx])
          float sum = 0;
          for (int j = 0; j < 32; j++) {
            int protoIdx = j * maskSize * maskSize + my * maskSize + mx;
            sum += maskCoeffs[j] * protoOutput[protoIdx];
          }

          // Sigmoid 并二值化
          float maskVal = sigmoid(sum);

          // 存储到裁剪后的 mask
          int dstIdx = (my - my1) * cropW + (mx - mx1);
          seg.mask[dstIdx] = (maskVal > 0.5f) ? 255 : 0;
        }
      }

      // bbox坐标保持不变（已在原图坐标系）
    }

    candidates.push_back(seg);
  }

  // NMS
  std::vector<WatermarkBBox> bboxes;
  bboxes.reserve(candidates.size());
  for (const auto& seg : candidates) {
    bboxes.push_back(seg.bbox);
  }
  auto keepIndices = nms(bboxes, nmsThresh);

  std::vector<WatermarkSeg> result;
  result.reserve(keepIndices.size());
  for (int idx : keepIndices) {
    result.push_back(candidates[idx]);
  }

  return result;
}

std::vector<WatermarkBBox> YOLODetector::postprocess(
    const float* outputData, int numAnchors, int channels,
    int origWidth, int origHeight,
    float scaleX, float scaleY,
    float confThresh, float nmsThresh) {

  std::vector<WatermarkBBox> candidates;

  // ONNX 输出格式: [anchors, channels]
  // channels = 38: 0-3=xyxy(bbox), 4=obj_conf, 5=class_conf(单类为0), 6-37=mask_coeffs
  // 对于单类检测，直接使用 obj_conf 作为置信度
  for (int i = 0; i < numAnchors; i++) {
    // 获取置信度 (单类检测直接用 obj_conf)
    float conf = outputData[i * channels + 4];

    // 过滤低置信度
    if (conf < confThresh) continue;

    // 获取边界框 (xyxy 格式: x1, y1, x2, y2) - 在模型输入空间 (640x640)
    float lx1 = outputData[i * channels + 0];
    float ly1 = outputData[i * channels + 1];
    float lx2 = outputData[i * channels + 2];
    float ly2 = outputData[i * channels + 3];

    // 映射回原图坐标 (直接 resize，分别用 scaleX 和 scaleY)
    float x1 = lx1 / scaleX;
    float y1 = ly1 / scaleY;
    float x2 = lx2 / scaleX;
    float y2 = ly2 / scaleY;

    // 计算宽高
    float w = x2 - x1;
    float h = y2 - y1;

    // 裁剪到图像范围
    x1 = std::max(0.0f, x1);
    y1 = std::max(0.0f, y1);
    w = std::min(w, origWidth - x1);
    h = std::min(h, origHeight - y1);

    WatermarkBBox bbox;
    bbox.x = x1;
    bbox.y = y1;
    bbox.width = w;
    bbox.height = h;
    bbox.confidence = conf;
    bbox.classId = 0;  // 单类检测

    candidates.push_back(bbox);
  }

  // NMS
  auto keepIndices = nms(candidates, nmsThresh);

  std::vector<WatermarkBBox> result;
  result.reserve(keepIndices.size());
  for (int idx : keepIndices) {
    result.push_back(candidates[idx]);
  }

  return result;
}

std::vector<int> YOLODetector::nms(const std::vector<WatermarkBBox>& boxes,
                                    float nmsThresh) {
  // 按置信度排序
  std::vector<int> indices(boxes.size());
  for (size_t i = 0; i < boxes.size(); i++) {
    indices[i] = static_cast<int>(i);
  }
  std::sort(indices.begin(), indices.end(),
            [&boxes](int a, int b) {
              return boxes[a].confidence > boxes[b].confidence;
            });

  std::vector<int> keep;
  std::vector<bool> suppressed(boxes.size(), false);

  for (int i : indices) {
    if (suppressed[i]) continue;

    keep.push_back(i);

    for (int j : indices) {
      if (suppressed[j] || i == j) continue;

      float iou = computeIoU(boxes[i], boxes[j]);
      if (iou > nmsThresh) {
        suppressed[j] = true;
      }
    }
  }

  return keep;
}

float YOLODetector::computeIoU(const WatermarkBBox& a, const WatermarkBBox& b) {
  float xx1 = std::max(a.x, b.x);
  float yy1 = std::max(a.y, b.y);
  float xx2 = std::min(a.x + a.width, b.x + b.width);
  float yy2 = std::min(a.y + a.height, b.y + b.height);

  float w = std::max(0.0f, xx2 - xx1);
  float h = std::max(0.0f, yy2 - yy1);
  float inter = w * h;

  float area1 = a.width * a.height;
  float area2 = b.width * b.height;
  float unionArea = area1 + area2 - inter;

  return (unionArea > 0) ? (inter / unionArea) : 0;
}

}
