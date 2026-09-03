#include "LamaInpainter.hpp"

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/LogHelper.hpp"

#include <opencv2/opencv.hpp>

namespace avox {

LamaInpainter::LamaInpainter() {}
LamaInpainter::~LamaInpainter() { close(); }

void LamaInpainter::close() {
  // session 借用自 OnnxSessionCache(Shared), 对象不释放模型; 仅清自身指针与缓冲
  session = nullptr;
  imageTensor.clear();
  maskTensor.clear();
  dilatedMask.clear();
  currentLevel = ModelLevel::none;
}

bool LamaInpainter::open(ModelLevel level, bool useGPU) {
  if (level == ModelLevel::none) {
    return false;
  }
  // 经基类 OnnxModelUser 从全局缓存取 (3 个 level 都映射 lama_base.onnx, 单 enum 值)
  session = OnnxModelUser::session(OnnxModel::Lama, useGPU);
  if (!session) {
    LOGFLF(LogLevel::error, "[LamaInpainter] ONNX session 加载失败");
    return false;
  }
  auto inputNames = session->getInputNames();
  auto outputNames = session->getOutputNames();
  LOGFLF(LogLevel::info, "[LamaInpainter::open] Inputs: ", inputNames.size());
  for (const auto& name : inputNames) {
    auto shape = session->getInputShape(name);
    std::string shapeStr;
    for (size_t i = 0; i < shape.size(); i++) {
      shapeStr += (i > 0 ? ", " : "") + std::to_string(shape[i]);
    }
    LOGFLF(LogLevel::info, "  ", name, ": [", shapeStr, "]");
  }
  LOGFLF(LogLevel::info, "[LamaInpainter::open] Outputs: ", outputNames.size());
  for (const auto& name : outputNames) {
    auto shape = session->getOutputShape(name);
    std::string shapeStr;
    for (size_t i = 0; i < shape.size(); i++) {
      shapeStr += (i > 0 ? ", " : "") + std::to_string(shape[i]);
    }
    LOGFLF(LogLevel::info, "  ", name, ": [", shapeStr, "]");
  }
  // 自动检测输入名称 - 根据模型自动获取
  if (inputNames.size() >= 2) {
    imageInputName = inputNames[0];  // 通常是第一个输入
    maskInputName = inputNames[1];   // 通常是第二个输入
  }
  LOGFLF(LogLevel::info, "[LamaInpainter] Using input names - image: ",
         imageInputName, ", mask: ", maskInputName);
  // 自动检测输出名称
  if (!outputNames.empty()) {
    outputName = outputNames[0];
  } else {
    outputName = "output";
  }
  LOGFLF(LogLevel::info, "[LamaInpainter] Using output name: ", outputName);
  // 获取输入尺寸
  auto shape = session->getInputShape(imageInputName);
  if (shape.size() >= 3 && shape[2] > 0) {
    inputSize = static_cast<int>(shape[2]);
  }
  currentLevel = level;
  return true;
}

void LamaInpainter::preprocess(const uint8_t* image,
                                const uint8_t* mask,
                                int width, int height) {
  // 1. 膨胀 mask (关键！解决细小水印修复效果差的问题)
  cv::Mat maskMat(height, width, CV_8UC1, const_cast<uint8_t*>(mask));
  dilatedMask.resize(width * height);
  cv::Mat dilated(height, width, CV_8UC1, dilatedMask.data());
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_RECT, cv::Size(dilateKernelSize, dilateKernelSize));
  cv::dilate(maskMat, dilated, kernel, cv::Point(-1, -1), dilateIterations);

  LOGFLF(LogLevel::info, "[LamaInpainter::preprocess] Dilated mask, kernel=",
         dilateKernelSize, "x", dilateKernelSize, ", iterations=", dilateIterations);

  // 2. resize 图像到 512x512 (LANCZOS4 对应 PIL 的 LANCZOS)
  cv::Mat imgMat(height, width, CV_8UC3, const_cast<uint8_t*>(image));
  cv::Mat imgResized;
  cv::resize(imgMat, imgResized, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_LANCZOS4);

  // 3. resize mask 到 512x512 (NEAREST 对应 PIL 的 NEAREST)
  cv::Mat maskResized;
  cv::resize(dilated, maskResized, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_NEAREST);

  // 4. 转换为 NCHW 格式并归一化
  size_t planeSize = inputSize * inputSize;
  imageTensor.resize(3 * planeSize);  // [3, 512, 512]
  maskTensor.resize(planeSize);       // [1, 512, 512]

  // 图像归一化到 0~1 (不是 -1~1!)
  cv::Mat imgFloat;
  imgResized.convertTo(imgFloat, CV_32F, 1.0 / 255.0);

  // 分离通道并复制到 NCHW 格式
  std::vector<cv::Mat> channels(3);
  cv::split(imgFloat, channels);
  std::memcpy(imageTensor.data(), channels[0].data, planeSize * sizeof(float));  // R
  std::memcpy(imageTensor.data() + planeSize, channels[1].data, planeSize * sizeof(float));  // G
  std::memcpy(imageTensor.data() + 2 * planeSize, channels[2].data, planeSize * sizeof(float));  // B

  // mask 二值化 (0或1), 1 = 需要修复
  for (int i = 0; i < inputSize * inputSize; i++) {
    maskTensor[i] = maskResized.data[i] > 128 ? 1.0f : 0.0f;
  }
}

void LamaInpainter::postprocess(const float* output,
                                 const uint8_t* original,
                                 const uint8_t* mask,
                                 int width, int height,
                                 uint8_t* result) {
  size_t planeSize = inputSize * inputSize;

  // 1. 从 NCHW 构建图像 [3, 512, 512]
  cv::Mat rPlane(inputSize, inputSize, CV_32F, const_cast<float*>(output));
  cv::Mat gPlane(inputSize, inputSize, CV_32F, const_cast<float*>(output + planeSize));
  cv::Mat bPlane(inputSize, inputSize, CV_32F, const_cast<float*>(output + 2 * planeSize));

  std::vector<cv::Mat> channels = {rPlane, gPlane, bPlane};
  cv::Mat imgOut;
  cv::merge(channels, imgOut);

  // 2. 输出范围是 0~255，直接转换
  cv::Mat imgUint8;
  imgOut.convertTo(imgUint8, CV_8U);
  // clip 到 0~255
  cv::Mat imgClipped;
  cv::threshold(imgUint8, imgClipped, 255, 255, cv::THRESH_TRUNC);
  cv::threshold(imgClipped, imgClipped, 0, 0, cv::THRESH_TOZERO);

  // 3. resize 回原图尺寸 (LANCZOS4)
  cv::Mat imgResized;
  cv::resize(imgClipped, imgResized, cv::Size(width, height), 0, 0, cv::INTER_LANCZOS4);

  // 4. 混合: mask 区域用修复结果, 非 mask 区域用原图
  // 使用膨胀后的 mask 进行混合
  cv::Mat origMat(height, width, CV_8UC3, const_cast<uint8_t*>(original));
  cv::Mat maskMat(height, width, CV_8UC1, dilatedMask.data());
  cv::Mat resultMat(height, width, CV_8UC3, result);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (maskMat.at<uint8_t>(y, x) > 128) {
        // mask 区域，使用修复结果
        resultMat.at<cv::Vec3b>(y, x) = imgResized.at<cv::Vec3b>(y, x);
      } else {
        // 非 mask 区域，保留原图
        resultMat.at<cv::Vec3b>(y, x) = origMat.at<cv::Vec3b>(y, x);
      }
    }
  }
}

bool LamaInpainter::inpaint(const uint8_t* rgbImage,
                             const uint8_t* mask,
                             int width, int height,
                             uint8_t* output) {
  if (!ready()) {
    LOGFLF(LogLevel::error, "[LamaInpainter::inpaint] Model not loaded");
    return false;
  }

  preprocess(rgbImage, mask, width, height);

  std::vector<std::pair<std::string, const float*>> inputs = {
      {imageInputName, imageTensor.data()},
      {maskInputName, maskTensor.data()}
  };

  std::vector<std::string> outputNames = {outputName};
  std::vector<std::vector<float>> outputs;

  if (!session->run(inputs, outputNames, outputs)) {
    LOGFLF(LogLevel::error, "[LamaInpainter::inpaint] ONNX inference failed");
    return false;
  }

  postprocess(outputs[0].data(), rgbImage, mask, width, height, output);
  return true;
}

}
