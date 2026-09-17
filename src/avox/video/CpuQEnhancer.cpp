#include "CpuQEnhancer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "avox/Avox.hpp"
#include "avox/vision/OnnxModel.hpp"
#include "avox/vision/OnnxModelUser.hpp"
#include "avox/vision/IOVEngine.hpp"
#include "avox/vision/IONNXSession.hpp"

namespace avox {

namespace {

// 行级并行(转换是逐行独立标量循环), 离线推理间隙的转换不串全场
void parallelFor(int32_t rows, const std::function<void(int32_t, int32_t)>& fn) {
  int32_t n = (int32_t)std::thread::hardware_concurrency();
  if (n <= 0) n = 4;
  int32_t step = std::max((rows + n - 1) / n, 1);
  std::vector<std::thread> pool;
  for (int32_t y0 = 0; y0 < rows; y0 += step) {
    int32_t y1 = std::min(rows, y0 + step);
    pool.emplace_back([=] { fn(y0, y1); });
  }
  for (auto& t : pool) {
    t.join();
  }
}

inline uint8_t clamp255(float v) {
  return (uint8_t)std::min(255.0f, std::max(0.0f, v));
}

bool g_serialPack = false;  // [dbg] 竞态排查: true=串行执行转换

}  // namespace

CpuQEnhancer::~CpuQEnhancer() {}

bool CpuQEnhancer::init(const QualityEnhanceParamet& p, int32_t sw,
                        int32_t sh, YuvType outType) {
  // [dbg] 竞态排查: ENH_NOINIT=1 不加载模型/后端, 走纯回退路径
  static const bool noInit = std::getenv("ENH_NOINIT") != nullptr;
  if (noInit) {
    return false;
  }
  paramet = p;
  srcW = sw;
  srcH = sh;
  yuvType = outType;
  // 倍率规则与图内层 calcOutputSize 一致: restore=1x, 2x, 4x, Auto按源分辨率
  int32_t mul = 2;
  if (paramet.outputMode == QualityOutputMode::Restore) {
    mul = 1;
  } else if (paramet.outputMode == QualityOutputMode::Upscale2x) {
    mul = 2;
  } else if (paramet.outputMode == QualityOutputMode::Upscale4x) {
    mul = scale;
  } else {
    mul = (srcW >= 1920 || srcH >= 1080) ? 2 : scale;
  }
  outW = srcW * mul;
  outH = srcH * mul;
  // 推理分辨率 = 输出/scale, x4 后恰好等于输出(restore=src/4 再 4x 回 src)
  inferW = outW / scale;
  inferH = outH / scale;
  if (inferW <= 0 || inferH <= 0 || outW > 7680 || outH > 4320) {
    LOGFLF(LogLevel::error, "CpuQEnhancer invalid size:", inferW, "x", inferH,
           " -> ", outW, "x", outH);
    return false;
  }
  inPlanar.resize((size_t)3 * inferW * inferH);
  outPlanar.resize((size_t)3 * outW * outH);
  rgbaTmp.resize((size_t)outW * outH * 4);
  size_t ySize = (size_t)outW * outH;
  size_t uvSize = (size_t)(outW / 2) * (outH / 2);
  if (yuvType == YuvType::nv12) {
    yuvBuf.resize(ySize + uvSize * 2);
  } else {
    yuvBuf.resize(ySize + uvSize * 2);
  }
  if (!loadModel()) {
    LOGFLF(LogLevel::error, "CpuQEnhancer load model failed, enhance off");
    return false;
  }
  LOGFLF(LogLevel::info, "CpuQEnhancer ready, infer=", inferW, "x", inferH,
         " out=", outW, "x", outH, " useOpenVino=", useOpenVino ? 1 : 0);
  return true;
}

bool CpuQEnhancer::loadModel() {
  std::string modelPath = getModelFilePath("quality/realesrgan-general-x4v3.onnx");
  ovEngine.reset(AvoxManager::Get().openvinoEngineHub.create("openvino"));
  if (ovEngine && ovEngine->loadModel(modelPath, inferH, inferW, scale)) {
    useOpenVino = true;
    modelLoaded = true;
    LOGFLF(LogLevel::info, "CpuQEnhancer: OpenVINO ", ovEngine->device());
    return true;
  }
  ovEngine.reset();
  OnnxModelUser user;
  onnxSession = user.session(OnnxModel::RealESRGanX4V3, false, 0, 8);
  if (!onnxSession) {
    return false;
  }
  auto inputNames = onnxSession->getInputNames();
  auto outputNames = onnxSession->getOutputNames();
  if (inputNames.empty() || outputNames.empty()) {
    onnxSession = nullptr;
    return false;
  }
  inputName = inputNames[0];
  outputName = outputNames[0];
  useOpenVino = false;
  modelLoaded = true;
  LOGFLF(LogLevel::info, "CpuQEnhancer: ORT CPU (fallback) input=", inputName);
  return true;
}

// rgba packed(带行距) → box 降采样到推理分辨率 → NCHW float [0,1]
void CpuQEnhancer::packPlanar(const uint8_t* src, int32_t pitch) {
  int32_t rx = std::max(srcW / inferW, 1);
  int32_t ry = std::max(srcH / inferH, 1);
  if (g_serialPack) {
    parallelFor(inferH, [&](int32_t y0, int32_t y1) { fnPack(y0, y1, src, pitch, rx, ry); });
    return;
  }
  fnPack(0, inferH, src, pitch, rx, ry);
}

void CpuQEnhancer::fnPack(int32_t y0, int32_t y1, const uint8_t* src,
                          int32_t pitch, int32_t rx, int32_t ry) {
    for (int32_t iy = y0; iy < y1; ++iy) {
      for (int32_t ix = 0; ix < inferW; ++ix) {
        int32_t x0 = ix * rx, yy0 = iy * ry;
        int32_t r = 0, g = 0, b = 0, cnt = 0;
        for (int32_t dy = 0; dy < ry; ++dy) {
          const uint8_t* row = src + (size_t)(yy0 + dy) * pitch;
          for (int32_t dx = 0; dx < rx; ++dx) {
            const uint8_t* px = row + (size_t)(x0 + dx) * 4;
            r += px[0];
            g += px[1];
            b += px[2];
            ++cnt;
          }
        }
        inPlanar[0 * inferW * inferH + (size_t)iy * inferW + ix] = r / (255.0f * cnt);
        inPlanar[1 * inferW * inferH + (size_t)iy * inferW + ix] = g / (255.0f * cnt);
        inPlanar[2 * inferW * inferH + (size_t)iy * inferW + ix] = b / (255.0f * cnt);
      }
    }
}

bool CpuQEnhancer::infer() {
  if (useOpenVino) {
    return ovEngine->infer(inPlanar.data(), outPlanar.data());
  }
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>> inputs = {
      {inputName, inPlanar.data(), {1, 3, inferH, inferW}}};
  std::vector<std::string> outNames = {outputName};
  std::vector<std::vector<float>> outputs;
  bool ok = onnxSession->runShaped(inputs, outNames, outputs);
  if (ok && !outputs.empty()) {
    std::copy(outputs[0].begin(), outputs[0].end(), outPlanar.begin());
  }
  return ok;
}

// NCHW float → rgba → yuv420p/nv12 (bt601 full, 与管线 yuv2rgba 矩阵一致)
void CpuQEnhancer::unpackToYuv(YUVFrame& out) {
  const int32_t w = outW, h = outH;
  // 解包+rgb→yuv 逐像素先落到 Y 平面, UV 按 2x2 块均值二次遍历
  parallelFor(h, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* yRow = yuvBuf.data() + (size_t)y * w;
      for (int32_t x = 0; x < w; ++x) {
        float rf = outPlanar[0 * w * h + (size_t)y * w + x] * 255.0f;
        float gf = outPlanar[1 * w * h + (size_t)y * w + x] * 255.0f;
        float bf = outPlanar[2 * w * h + (size_t)y * w + x] * 255.0f;
        uint8_t r = clamp255(rf);
        uint8_t g = clamp255(gf);
        uint8_t b = clamp255(bf);
        uint8_t yy = clamp255(0.299f * r + 0.587f * g + 0.114f * b);
        yRow[x] = yy;
        uint8_t* px = rgbaTmp.data() + (size_t)y * w * 4 + x * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
      }
    }
  });
  int32_t uw = w / 2, uh = h / 2;
  parallelFor(uh, [&](int32_t y0, int32_t y1) {
    for (int32_t uvY = y0; uvY < y1; ++uvY) {
      for (int32_t uvX = 0; uvX < uw; ++uvX) {
        int32_t r = 0, g = 0, b = 0;
        for (int32_t dy = 0; dy < 2; ++dy) {
          const uint8_t* row = rgbaTmp.data() + (size_t)(uvY * 2 + dy) * w * 4 + uvX * 8;
          r += row[0] + row[4];
          g += row[1] + row[5];
          b += row[2] + row[6];
        }
        r /= 4;
        g /= 4;
        b /= 4;
        uint8_t u = clamp255((b - r) / 1.772f + 128.0f);
        uint8_t v = clamp255((r - b) / 1.402f + 128.0f);
        if (yuvType == YuvType::nv12) {
          uint8_t* uv = yuvBuf.data() + (size_t)w * h + (size_t)uvY * uw * 2 + uvX * 2;
          uv[0] = u;
          uv[1] = v;
        } else {
          yuvBuf.data()[(size_t)w * h + (size_t)uvY * uw + uvX] = u;
          yuvBuf.data()[(size_t)w * h + (size_t)uw * uh + (size_t)uvY * uw + uvX] = v;
        }
      }
    }
  });
  out = {};
  out.pts = outFrame.pts;
  out.dts = outFrame.dts;
  out.keyFrame = outFrame.keyFrame;
  out.format.width = w;
  out.format.height = h;
  out.format.type = yuvType;
  out.data[0] = yuvBuf.data();
  out.stride[0] = w;
  if (yuvType == YuvType::nv12) {
    out.data[1] = yuvBuf.data() + (size_t)w * h;
    out.stride[1] = w;
  } else {
    out.data[1] = yuvBuf.data() + (size_t)w * h;
    out.data[2] = yuvBuf.data() + (size_t)w * h + (size_t)uw * uh;
    out.stride[1] = uw;
    out.stride[2] = uw;
  }
}

bool CpuQEnhancer::process(IImageBuffer* rgba, int64_t pts, int64_t dts,
                           YUVFrame& out) {
  if (!modelLoaded) {
    return false;
  }
  ImageFormat fmt = rgba->getImageFormat();
  int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;
  outFrame.pts = pts;
  outFrame.dts = dts;
  // [dbg] 竞态排查: ENH_NOINFER=1 跳过转换与推理, 仅出黑帧走完管线
  static const bool noInfer = std::getenv("ENH_NOINFER") != nullptr;
  if (noInfer) {
    std::fill(yuvBuf.begin(), yuvBuf.end(), 0);
    out = {};
    out.pts = pts;
    out.dts = dts;
    out.format.width = outW;
    out.format.height = outH;
    out.format.type = yuvType;
    out.data[0] = yuvBuf.data();
    out.stride[0] = outW;
    if (yuvType == YuvType::nv12) {
      out.data[1] = yuvBuf.data() + (size_t)outW * outH;
      out.stride[1] = outW;
    } else {
      out.data[1] = yuvBuf.data() + (size_t)outW * outH;
      out.data[2] =
          yuvBuf.data() + (size_t)outW * outH + (size_t)(outW / 2) * (outH / 2);
      out.stride[1] = outW / 2;
      out.stride[2] = outW / 2;
    }
    return true;
  }
  // 首帧打印一次输入规格, 分段耗时每60帧打一次(逐帧刷屏无益)
  static std::atomic<int32_t> frameCnt{0};
  const int32_t frameIdx = frameCnt.fetch_add(1);
  if (frameIdx == 0) {
    LOGFLF(LogLevel::info, "CpuQEnhancer process input ", fmt.width, "x",
           fmt.height, " pitch=", pitch);
  }
  auto t0 = std::chrono::steady_clock::now();
  packPlanar(rgba->getPointer(), pitch);
  auto t1 = std::chrono::steady_clock::now();
  bool ok = infer();
  auto t2 = std::chrono::steady_clock::now();
  if (!ok) {
    LOGFLF(LogLevel::error, "CpuQEnhancer infer failed, frame dropped");
    return false;
  }
  unpackToYuv(out);
  auto t3 = std::chrono::steady_clock::now();
  auto ms = [](auto a, auto b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
  };
  if (frameIdx % 60 == 0) {
    LOGFLF(LogLevel::info, "CpuQEnhancer frame #", frameIdx, ":",
           ms(t0, t1), "+", ms(t1, t2), "+", ms(t2, t3),
           "ms (pack+infer+unpack)");
  }
  return true;
}

}
