# OCR 文字识别 (Text Recognition)

在场景图 (`IImageBuffer`) 中识别文字及其位置, 供 Agent 按文字查找并点击。
选型 **PP-OCRv6** (轻量 pipeline, 识别模块 ~5M, CPU 可跑, OmniDocBench v1.6 ≈ 96.3%)。
算法/后处理参考 [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx); 推理复用本仓 `avox_onnx`。

---

## 接口 (`src/avox/AvoxVision.h`, 与 ITemplateMatcher 同文件新开一节)

```cpp
// 单条 OCR 结果 (POD, 跨 DLL 安全)
struct OcrResult {
  int32_t x = 0, y = 0, w = 0, h = 0;  // 文字框 (scene 坐标系, 左上角 + 宽高)
  double score = 0.0;                  // 识别置信度 [0,1]
};
// 框中心 (Agent 点击用): vec2i getOcrCenter(const OcrResult& r)

// OCR 文字识别接口 (拉模式, 同 ITemplateMatcher: recognize 一次, getMatch 循环取)
class ITextRecognizer {
 public:
  virtual ~ITextRecognizer() = default;
  // 配置
  virtual void setThreshold(double) = 0;                 // 检测阈值, 默认 0.3
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;  // 默认整图
  virtual void clearRoi() = 0;
  virtual void setUseGpu(bool enable) = 0;               // 默认 CPU
  // 执行 + 结果
  virtual int32_t recognize(IImageBuffer* scene) = 0;     // 返回文字框数
  virtual int32_t getMatchCount() = 0;
  virtual const char* getMatch(int32_t index, OcrResult* out) = 0;  // 返回文字串, out 非空时写入框坐标; 越界 nullptr
  virtual float getMatchTimeMs() = 0;
  virtual const char* getLastError() = 0;
};
```

获取实例: `createTextRecognizer()` (内部走 `textRecognizerHub.create("ppocr")`, avox_ocr 未启用返回 nullptr 优雅降级)。

---

## 算法流程 (det → rec, cls 默认关)

1. **通道统一** scene 经自带 `bufferToBgr` 转 BGR(`CV_8UC3`) (avox_ocr 自包含, 不依赖 avox_opencv; 直接用 `IImageBuffer::getPointer` 构造 `cv::Mat` + `cvtColor`)。
2. **检测 det**: DBNet。预处理(限长边 960 + 对齐 32 + ImageNet 归一化) → `IONNXSession::runShaped` (det 输入 [1,3,H,W] 动态 H/W, 显式指定 shape) → 概率图 → 二值化 → `findContours` → boundingRect + unclip → 文字框列表。
3. **识别 rec**: CRNN + CTC。逐框从 scene 裁出 → **BGR→RGB** (PP-OCRv6 rec 训练用 RGB) → 预处理(高 48, 宽按比例, 归一化 0.5/0.5) → `IONNXSession::runShaped` → CTC 解码 + 字典查表 → UTF-8 文字串。**字典须匹配模型**(PP-OCRv6 ≈18709 字符, 从模型 `inference.yml` 的 `character_dict` 提取)。
4. **cls 方向分类默认关闭**: 屏幕文字一般正向, 跳过以提速。
5. **结果缓存**: `recognize` 后结果存内部向量, `getMatch`/`getText` 按索引取 (同 `ITemplateMatcher`)。
6. **坐标** 始终相对 scene 左上角; ROI 仅缩小检测范围, 文字框已映射回 scene。
7. **线程** 实例非线程安全 (多线程各自 `create`)。

---

## 用法

```cpp
#include "avox/AvoxVision.h"
using namespace avox;

ITextRecognizer* raw = createTextRecognizer();             // nullptr = avox_ocr 未启用
std::unique_ptr<ITextRecognizer> ocr(raw);  // 模型首次 recognize 时按需加载 (assets/models/ocr/)

IImageBuffer* scene = createImageBuffer();
player->getSurfaceRender()->screenShot(scene);             // 截图 (RGBA)
std::unique_ptr<IImageBuffer> guard(scene);

int n = ocr->recognize(scene);
for (int i = 0; i < n; ++i) {
  OcrResult r;
  const char* text = ocr->getMatch(i, &r);
  // text="开始", vec2i c = getOcrCenter(r) → agent.click(c.x, c.y);
}
```

demo: `samples/functest/ocrttest.cpp`。

---

## 关键文件

| 用途 | 路径 |
|------|------|
| 接口 + 自由函数 | `src/avox/AvoxVision.h` / `src/avox/vision/Vision.cpp` (`createTextRecognizer` / `getMatchCenter` / `getOcrCenter`) |
| 工厂注册点 | `src/avox/module/AvoxManager.hpp` (`textRecognizerHub`) |
| PP-OCRv6 实现 | `plugins/avox_ocr/TextRecognizer.{hpp,cpp}`、`OcrModule.cpp` |
| 推理引擎 | `plugins/avox_onnx/ONNXRuntime.{hpp,cpp}` (复用 + `runShaped` 增强) |
| 后处理参考 | [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx) (det 轮廓/rec CTC 解码移植) |
| 模型文件 | 外部库 (不进 git): `ch_PP-OCRv6_det.onnx` / `_rec.onnx` / `ppocr_keys_v1.txt` (≈18709 字符) |
| demo | `samples/functest/ocrttest.cpp` |
| 构建开关 | `AVOX_ENABLE_OCR` (`plugins/options.cmake`, 默认 ON, 依赖 `AVOX_ENABLE_ONNX`; 库缺失自动跳过) |

跨 DLL 安全: 接口仅传 `IImageBuffer*` + 原始类型 + POD `OcrResult`; 文字串用 `getText` 两段式拿 (写入调用方 buf, 不传 STL)。详见 `doc/plan/动态加载组件设计.md`。
