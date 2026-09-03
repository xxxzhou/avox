#include "OVEngine.hpp"

#include <cstring>
#include <memory>

#include <openvino/openvino.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

// ov::Core 单例: 首次构造加载 plugin/扫描设备(慢且吃内存), 全局复用。
// 多个 OVEngine 实例(VkQEnhanceLayer 重建)共享同一个 Core。
static ov::Core& sharedCore() {
  static ov::Core core;
  return core;
}

struct OVEngine::Impl {
  std::shared_ptr<ov::CompiledModel> compiled;  // 持有, 保证 InferRequest 有效
  ov::InferRequest request;
  int inferWidth = 0;
  int inferHeight = 0;
  int scale = 4;
  std::string device;
};

OVEngine::OVEngine() : impl_(std::make_unique<Impl>()) {}
OVEngine::~OVEngine() {
  // 显式释放: Impl 析构 → CompiledModel/InferRequest → OpenVINO GPU OpenCL 资源
  impl_.reset();
}

// 在指定 device 上编译模型 (reshape 推理分辨率 [1,3,H,W])。
// GPU: FP16(Intel iGPU 最快); CPU: FP32(python 实测 CPU FP16 反慢)。
// 分步 try/catch(...): /EHa 下 catch(...) 能捕获 SEH 硬件异常(avox 已用 Vulkan 占住 GPU,
// OpenVINO init 可能抢 driver → access violation); 任一步失败即降级到下一 device/ORT。
bool OVEngine::tryCompile(const std::string& modelPath, int H, int W, int scale,
                          const std::string& dev) {
  // ① read_model: 首次经 sharedCore() 构造 ov::Core(加载 plugin/扫描设备/初始化 TBB)
  std::shared_ptr<ov::Model> model;
  try {
    model = sharedCore().read_model(modelPath);
  } catch (...) {
    LOGFLF(LogLevel::error, "OV ", dev, ": read_model failed (Core init/ONNX parse)");
    return false;
  }
  // ② reshape: 动态 H/W → 固定推理分辨率 [1,3,H,W] (output 自动推导)
  try {
    model->reshape(std::map<ov::Output<ov::Node>, ov::PartialShape>{
        {model->input(), ov::PartialShape{ov::Dimension{1}, ov::Dimension{3},
                                          ov::Dimension{H}, ov::Dimension{W}}}});
  } catch (...) {
    LOGFLF(LogLevel::error, "OV ", dev, ": reshape failed");
    return false;
  }
  // ③ compile_model + create_infer_request: GPU 用 FP16
  try {
    std::shared_ptr<ov::CompiledModel> compiled;
    if (dev == "GPU") {
      compiled = std::make_shared<ov::CompiledModel>(sharedCore().compile_model(
          model, dev,
          ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY),
          ov::hint::inference_precision(ov::element::f16)));
    } else {
      compiled = std::make_shared<ov::CompiledModel>(sharedCore().compile_model(
          model, dev,
          ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)));
    }
    impl_->compiled = compiled;
    impl_->request = compiled->create_infer_request();
    impl_->inferWidth = W;
    impl_->inferHeight = H;
    impl_->scale = scale;
    impl_->device = dev;
    return true;
  } catch (...) {
    LOGFLF(LogLevel::error, "OV ", dev, ": compile_model failed (device conflict?)");
    return false;
  }
}

bool OVEngine::loadModel(const std::string& modelPath,
                         int inferHeight, int inferWidth, int scale) {
  // 运行期降级链: ① GPU(Intel iGPU FP16, 实测 ~131ms) → ② CPU(FP32, ~630ms) → ③ false(调用方走 ORT)
  if (tryCompile(modelPath, inferHeight, inferWidth, scale, "GPU")) {
    LOGFLF(LogLevel::info, "OpenVINO loaded on GPU(FP16) infer ",
           inferWidth, "x", inferHeight);
    return true;
  }
  if (tryCompile(modelPath, inferHeight, inferWidth, scale, "CPU")) {
    LOGFLF(LogLevel::info, "OpenVINO loaded on CPU(FP32) infer ",
           inferWidth, "x", inferHeight);
    return true;
  }
  LOGFLF(LogLevel::warn, "OpenVINO: GPU & CPU both failed");
  return false;
}

bool OVEngine::infer(const float* input, float* output) {
  try {
    // 套外部 buffer 不拷贝 (input 来自 preprocessBuffer map)
    ov::Shape inShape{static_cast<size_t>(1), static_cast<size_t>(3),
                      static_cast<size_t>(impl_->inferHeight),
                      static_cast<size_t>(impl_->inferWidth)};
    ov::Tensor inT(ov::element::f32, inShape, const_cast<float*>(input));
    impl_->request.set_input_tensor(inT);
    impl_->request.infer();
    ov::Tensor outT = impl_->request.get_output_tensor();
    const float* out = outT.data<float>();
    memcpy(output, out, outT.get_size() * sizeof(float));
    return true;
  } catch (...) {
    LOGFLF(LogLevel::error, "OpenVINO infer SEH/exception");
    return false;
  }
}

std::string OVEngine::device() const { return impl_->device; }

}
