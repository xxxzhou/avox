#include "AOTGanInpainter.hpp"

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/LogHelper.hpp"

#include <opencv2/opencv.hpp>

namespace avox {

AOTGanInpainter::AOTGanInpainter() {}
AOTGanInpainter::~AOTGanInpainter() { close(); }

void AOTGanInpainter::close() {
  // session 借用自 OnnxSessionCache(Shared), 对象不释放模型; 仅清自身指针与缓冲
  session = nullptr;
  imageTensor.clear();
  maskTensor.clear();
  dilatedMask.clear();
}

bool AOTGanInpainter::open(bool useGPU) {
  // 经基类 OnnxModelUser 从全局缓存取
  session = OnnxModelUser::session(OnnxModel::Aotgan, useGPU);
  if (!session) {
    LOGFLF(LogLevel::error, "[AOTGanInpainter] ONNX session 加载失败");
    return false;
  }
  auto inputNames = session->getInputNames();
  auto outputNames = session->getOutputNames();
  LOGFLF(LogLevel::info, "[AOTGanInpainter::open] Inputs: ", inputNames.size());
  for (const auto& name : inputNames) {
    auto shape = session->getInputShape(name);
    std::string shapeStr;
    for (size_t i = 0; i < shape.size(); i++) {
      shapeStr += (i > 0 ? ", " : "") + std::to_string(shape[i]);
    }
    LOGFLF(LogLevel::info, "  ", name, ": [", shapeStr, "]");
  }
  LOGFLF(LogLevel::info, "[AOTGanInpainter::open] Outputs: ", outputNames.size());
  for (const auto& name : outputNames) {
    auto shape = session->getOutputShape(name);
    std::string shapeStr;
    for (size_t i = 0; i < shape.size(); i++) {
      shapeStr += (i > 0 ? ", " : "") + std::to_string(shape[i]);
    }
    LOGFLF(LogLevel::info, "  ", name, ": [", shapeStr, "]");
  }
  // 从模型动态获取输入输出名称 (与 LaMa 相同的方式)
  if (inputNames.size() >= 2) {
    imageInputName = inputNames[0];
    maskInputName = inputNames[1];
  } else {
    LOGFLF(LogLevel::error, "[AOTGanInpainter::open] Invalid input count: ", inputNames.size());
    return false;
  }
  if (!outputNames.empty()) {
    outputName = outputNames[0];
  } else {
    outputName = "painted_image";
  }
  LOGFLF(LogLevel::info, "[AOTGanInpainter] Using input names - image: ",
         imageInputName, ", mask: ", maskInputName);
  LOGFLF(LogLevel::info, "[AOTGanInpainter] Using output name: ", outputName);
  // 获取输入尺寸
  auto shape = session->getInputShape(imageInputName);
  if (shape.size() >= 3 && shape[2] > 0) {
    inputSize = static_cast<int>(shape[2]);
  }
  return true;
}

void AOTGanInpainter::preprocess(const uint8_t* image,
                                  const uint8_t* mask,
                                  int width, int height) {
  // 1. 膨胀 mask
  cv::Mat maskMat(height, width, CV_8UC1, const_cast<uint8_t*>(mask));
  dilatedMask.resize(width * height);
  cv::Mat dilated(height, width, CV_8UC1, dilatedMask.data());
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_RECT, cv::Size(dilateKernelSize, dilateKernelSize));
  cv::dilate(maskMat, dilated, kernel, cv::Point(-1, -1), dilateIterations);

  LOGFLF(LogLevel::info, "[AOTGanInpainter::preprocess] Dilated mask, kernel=",
         dilateKernelSize, "x", dilateKernelSize, ", iterations=", dilateIterations);

  // 2. resize 图像到 512x512
  cv::Mat imgMat(height, width, CV_8UC3, const_cast<uint8_t*>(image));
  cv::Mat imgResized;
  cv::resize(imgMat, imgResized, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_LANCZOS4);

  // 3. resize mask 到 512x512
  cv::Mat maskResized;
  cv::resize(dilated, maskResized, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_NEAREST);

  // 4. 转换为 NCHW 格式并归一化 (包含批次维度)
  size_t planeSize = inputSize * inputSize;
  imageTensor.resize(1 * 3 * planeSize);  // [1, 3, 512, 512]
  maskTensor.resize(1 * planeSize);        // [1, 1, 512, 512]

  // 图像归一化到 0~1
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

void AOTGanInpainter::postprocess(const float* output,
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

  // 2. 输出范围检测并转换到 0~255
  double outMin, outMax;
  cv::minMaxLoc(imgOut, &outMin, &outMax);

  cv::Mat imgScaled;
  if (outMax <= 1.0f && outMin >= 0.0f) {
    // 输出是 0~1 范围，需要乘以 255 转成 0~255
    imgOut.convertTo(imgScaled, CV_32F, 255.0);
  } else if (outMin >= -1.5f && outMax <= 1.5f) {
    // 输出是 -1~1 范围，转换到 0~255: (x + 1) * 127.5
    imgOut.convertTo(imgScaled, CV_32F, 127.5f, 127.5f);
  } else {
    // 假设是 0~255 范围，直接转换
    imgOut.convertTo(imgScaled, CV_32F);
  }

  // 转换到 8 位
  cv::Mat imgUint8;
  imgScaled.convertTo(imgUint8, CV_8U);
  // clip 到 0~255
  cv::Mat imgClipped;
  cv::threshold(imgUint8, imgClipped, 255, 255, cv::THRESH_TRUNC);
  cv::threshold(imgClipped, imgClipped, 0, 0, cv::THRESH_TOZERO);

  // 3. resize 回原图尺寸
  cv::Mat imgResized;
  cv::resize(imgClipped, imgResized, cv::Size(width, height), 0, 0, cv::INTER_LANCZOS4);

  // 4. 混合: mask 区域用修复结果, 非 mask 区域用原图
  cv::Mat origMat(height, width, CV_8UC3, const_cast<uint8_t*>(original));
  cv::Mat maskMat(height, width, CV_8UC1, dilatedMask.data());
  cv::Mat resultMat(height, width, CV_8UC3, result);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (maskMat.at<uint8_t>(y, x) > 128) {
        resultMat.at<cv::Vec3b>(y, x) = imgResized.at<cv::Vec3b>(y, x);
      } else {
        resultMat.at<cv::Vec3b>(y, x) = origMat.at<cv::Vec3b>(y, x);
      }
    }
  }
}

bool AOTGanInpainter::inpaint(const uint8_t* rgbImage,
                              const uint8_t* mask,
                              int width, int height,
                              uint8_t* output) {
  if (!ready()) {
    LOGFLF(LogLevel::error, "[AOTGanInpainter::inpaint] Model not loaded");
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
    LOGFLF(LogLevel::error, "[AOTGanInpainter::inpaint] ONNX inference failed");
    return false;
  }

  postprocess(outputs[0].data(), rgbImage, mask, width, height, output);
  return true;
}

}