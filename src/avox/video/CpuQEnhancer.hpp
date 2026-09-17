#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ImageBuffer.hpp"
#include "avox/AvoxLayer.h"

namespace avox {

class IOVEngine;
class IONNXSession;

// 离线画质增强器 (转码录制器专用; MediaPlayer/实时轨走图内 VkQEnhanceLayer, 不经过此处)
// 架构: Vulkan 管线出 rgba CPU 帧 → vFrameQueue(满则反压解码) → 编码线程侧
// 本器逐帧: rgba 打包 NCHW → Real-ESRGAN 推理(OpenVINO GPU→ORT CPU 降级)
// → 解包转 yuv。推理慢时整线按推理速度节拍, 天然逐帧有序, 无渲染图内时序补丁。
class CpuQEnhancer {
 public:
  CpuQEnhancer() = default;
  ~CpuQEnhancer();

 public:
  // 按 outputMode 计算倍率(Auto 规则同图内层)并加载模型; 无后端/无模型返回 false
  bool init(const QualityEnhanceParamet& paramet, int32_t srcWidth,
            int32_t srcHeight, YuvType outType);
  // 输入 rgba8 packed 帧(行距按 rowPitch), 输出写内部缓冲(下次 process 失效)
  bool process(IImageBuffer* rgba, int64_t pts, int64_t dts, YUVFrame& out);
  int32_t outWidth() const { return outW; }
  int32_t outHeight() const { return outH; }
  // 推理分辨率(图侧 enableImage 缓冲按此设, GPU resize 直接到推理大小, CPU 免降采样)
  int32_t inferWidth() const { return inferW; }
  int32_t inferHeight() const { return inferH; }

 private:
  bool loadModel();
  void packPlanar(const uint8_t* src, int32_t pitch);
  void fnPack(int32_t y0, int32_t y1, const uint8_t* src, int32_t pitch,
              int32_t rx, int32_t ry);
  bool infer();
  void unpackToYuv(YUVFrame& out);

 private:
  std::unique_ptr<IOVEngine> ovEngine;
  IONNXSession* onnxSession = nullptr;  // OnnxSessionCache 借用, 不 delete
  bool useOpenVino = false;
  bool modelLoaded = false;
  std::string inputName, outputName;
  int32_t scale = 4;
  int32_t srcW = 0, srcH = 0;
  int32_t inferW = 0, inferH = 0;
  int32_t outW = 0, outH = 0;
  YuvType yuvType = YuvType::yuv420P;
  QualityEnhanceParamet paramet = {};
  // 打包/推理/解包缓冲
  std::vector<float> inPlanar;
  std::vector<float> outPlanar;
  std::vector<uint8_t> rgbaTmp;
  std::vector<uint8_t> yuvBuf;
  YUVFrame outFrame = {};
};

}
