# YOLO26-pose 人体检测设计文档

## 概述

使用 YOLO26-pose 轻量级人体姿态估计模型对 `IImageBuffer` 进行逐帧检测，识别画面中的人数、每个人的边界框（bbox）、17 个 COCO 关键点，并基于关键点几何关系推断动作（站立/坐下/躺下）。通过比较前后帧的检测结果，输出事件：

- 人数变化（`PersonCountChange`）
- 动作变化（`ActionChange`）

## 模块位置

```
src/avox_analysis/
├── CNNTypes.hpp        # 类型定义
├── YOLODetector.hpp    # YOLO26-pose 检测器声明
└── YOLODetector.cpp    # 实现

assets/models/
└── yolo26n-pose.onnx   # YOLO26-nano pose 模型
```

## 模型

### YOLO26-pose

| 项目 | 说明 |
|------|------|
| 参数量 | ~3.5M (nano) |
| 输入 | RGB, 640×640, NCHW, 归一化 [0, 1] |
| ONNX 输出 | `[1, 56, 8400]` |
| 检测目标 | person（仅人物类） |

**输出格式**：
```
56 = 4(bbox: cx,cy,w,h) + 1(obj conf) + 51(17关键点 × 3)
8400 = anchor 总数 (80×80 + 40×40 + 20×20)
```

### COCO 17 关键点

```
 0: nose          1: left_eye       2: right_eye
 3: left_ear      4: right_ear
 5: left_shoulder 6: right_shoulder
 7: left_elbow    8: right_elbow
 9: left_wrist   10: right_wrist
11: left_hip     12: right_hip
13: left_knee    14: right_knee
15: left_ankle   16: right_ankle
```

### 模型选择

| 模型 | 参数量 | 推理速度 (CPU) | 推理速度 (GPU) | 适用场景 |
|------|--------|----------------|----------------|----------|
| YOLO26n-pose | ~3.5M | 25+ FPS | 100+ FPS | 实时检测，资源受限 |
| YOLO26s-pose | ~12M | 15+ FPS | 80+ FPS | 平衡精度与速度 |
| YOLO26m-pose | ~27M | 8+ FPS | 50+ FPS | 精度优先 |

**推荐**: 默认使用 YOLO26n-pose (nano)。

## 核心类型

### CNNEventType

```cpp
enum class CNNEventType {
  PersonCountChange,   // 人数变化
  ActionChange         // 动作变化（站立/坐下/躺下）
};
```

只保留 pose 模型能真正检测的事件。`ObjectAppear`、`ObjectDisappear`、`SceneChange` 不在当前范围内。

### 检测相关结构

```cpp
// 单个关键点
struct KeyPoint {
  float x = 0;
  float y = 0;
  float confidence = 0;
};

// 单个人物检测结果
struct PersonDetection {
  float x = 0, y = 0, width = 0, height = 0;   // bbox（原图坐标）
  float confidence = 0;
  KeyPoint keypoints[17];
  PersonAction action = PersonAction::Unknown;
};

// 单帧检测结果
struct CNNDetectResult {
  int personCount = 0;
  std::vector<PersonDetection> persons;
};
```

### 动作标签

```cpp
enum class PersonAction {
  Unknown,
  Standing,    // 躯干垂直，腿伸直（hip 明显在 knee 上方）
  Sitting,     // 躯干垂直，膝盖弯曲（hip 与 knee y 坐标接近）
  Lying        // 躯干接近水平（肩和髋 y 坐标接近）
};
```

`Walking` 暂不包含——单帧关键点难以可靠区分站立和行走，后续可结合帧间位移判断。

### 配置

```cpp
struct CNNConfig {
  std::string modelPath;            // ONNX 模型路径
  int inputWidth = 640;
  int inputHeight = 640;
  float confThreshold = 0.25f;      // bbox 置信度阈值
  float nmsThreshold = 0.45f;       // NMS 阈值
  float keypointThreshold = 0.5f;   // 关键点置信度阈值
};
```

## YOLODetector 类

```cpp
class YOLODetector {
 public:
  bool load(const std::string& modelPath, const CNNConfig& config);
  bool isLoaded() const;
  void unload();
  void reset();  // 清除上一帧缓存

  // 对单张 IImageBuffer 做检测，返回 bbox + 关键点 + 动作
  CNNDetectResult detect(IImageBuffer* image);

  // 比较前后两帧结果，返回触发的事件列表
  std::vector<CNNEventType> detectEvents(const CNNDetectResult& prev,
                                         const CNNDetectResult& curr);

 private:
  ONNXSession session;
  CNNConfig config;
  std::string imageInputName;
  std::vector<float> preprocessed;
  CNNDetectResult lastResult;

  // 预处理：IImageBuffer → NCHW float（letterbox + 归一化）
  void preprocess(IImageBuffer* image);

  // 后处理：ONNX 输出 → PersonDetection 列表（含 NMS + 关键点映射 + 动作分类）
  std::vector<PersonDetection> postprocess(
      const float* output, int numAnchors, int channels,
      int origW, int origH);

  // 动作分类
  std::vector<PersonAction> classifyActions(
      const std::vector<PersonDetection>& persons);
  PersonAction classifySingleAction(const KeyPoint kp[17]);
  PersonAction classifyPose(const KeyPoint kp[17]);

  // NMS
  std::vector<int> nms(const std::vector<PersonDetection>& boxes,
                       float threshold);
  static float computeIoU(const PersonDetection& a, const PersonDetection& b);
};
```

## 预处理

输入 `IImageBuffer*`，可能的像素格式：`rgba8`、`bgra8`、`rgb8`、`bgr8`。

```
1. 根据 ImageType 提取 RGB 三通道
2. Letterbox 缩放：保持宽高比，缩放后填入 640×640，空白区域填灰度 114
3. NCHW 排列，归一化到 [0, 1]
```

```cpp
void preprocess(IImageBuffer* image) {
  auto fmt = image->getImageFormat();
  uint8_t* src = image->getPointer();

  // letterbox 参数
  float scale = std::min((float)inputW / fmt.width, (float)inputH / fmt.height);
  int newW = (int)(fmt.width * scale);
  int newH = (int)(fmt.height * scale);
  int padX = (inputW - newW) / 2;
  int padY = (inputH - newH) / 2;

  // 填充灰度 114/255，然后填入缩放后的 RGB 像素
  // 存入 preprocessed: [R_plane | G_plane | B_plane]
}
```

## 后处理

### 解析 ONNX 输出

```
输出: [1, 56, 8400] → 每 anchor 56 个值

float cx     = anchor[0];           // bbox 中心 x (模型输入空间)
float cy     = anchor[1];           // bbox 中心 y
float w      = anchor[2];           // bbox 宽
float h      = anchor[3];           // bbox 高
float conf   = anchor[4];           // 目标置信度

// 17 个关键点
for (int k = 0; k < 17; k++) {
  float kx = anchor[5 + k * 3 + 0];  // 关键点 x (模型输入空间)
  float ky = anchor[5 + k * 3 + 1];  // 关键点 y
  float kc = anchor[5 + k * 3 + 2];  // 关键点置信度
}
```

### 坐标还原

```
1. bbox cx,cy,w,h → xyxy
2. 从模型输入空间映射回 letterbox 空间，再映射回原图空间
3. 关键点同样映射回原图空间
4. 过滤 keypoint.confidence < config.keypointThreshold 的关键点（置零）
```

### NMS

标准 NMS，按置信度降序排列，贪婪抑制 IoU > nmsThreshold 的重复框。

## 动作分类

### classifySingleAction

基于关键点几何关系判断单个人的动作：

```cpp
PersonAction classifyPose(const KeyPoint kp[17]) {
  // 计算需要的中心点（只使用高置信度的关键点）
  vec2 shoulder = avg(kp[5], kp[6]);   // 左肩+右肩中心
  vec2 hip      = avg(kp[11], kp[12]); // 左髋+右髋中心
  vec2 knee     = avg(kp[13], kp[14]); // 左膝+右膝中心

  if (shoulder 或 hip 关键点缺失) return Unknown;

  float torsoAngle = abs(atan2(hip.y - shoulder.y, hip.x - shoulder.x));
  float hipKneeDy = knee.y - hip.y;  // >0 膝盖在髋下方

  // 躺下: 躯干接近水平 (>60° 偏离垂直)
  if (torsoAngle > PI / 3) return Lying;

  // 坐下: 躯干垂直，但膝盖弯曲（膝盖与髋部 y 接近）
  if (hipKneeDy < threshold) return Sitting;

  // 站立: 躯干垂直，腿伸直
  return Standing;
}
```

判断阈值和所需关键点对在后面"待确认事项"中列出。

## 事件检测

```cpp
std::vector<CNNEventType> detectEvents(const CNNDetectResult& prev,
                                       const CNNDetectResult& curr) {
  std::vector<CNNEventType> events;

  // 1. 人数变化
  if (prev.personCount != curr.personCount) {
    events.push_back(CNNEventType::PersonCountChange);
  }

  // 2. 动作变化：比较每个人的 action 标签
  if (prev.personCount == curr.personCount && prev.personCount > 0) {
    // 按 bbox 位置就近匹配前后帧的人
    for (size_t i = 0; i < prev.persons.size() && i < curr.persons.size(); i++) {
      if (prev.persons[i].action != curr.persons[i].action) {
        events.push_back(CNNEventType::ActionChange);
        break;
      }
    }
  }

  return events;
}
```

人数变化时不同时报告 ActionChange（人数变了动作比较无意义）。

## CPU 推理

```cpp
// ONNXSession 使用 CPU 推理
session.loadModel(modelPath, /*useGPU=*/false, /*deviceId=*/0, /*numThreads=*/4);
```

## 构建系统

顶层 `CMakeLists.txt`:
```cmake
option(AVOX_ENABLE_ANALYSIS "build video analysis (YOLO-pose detection)" ON)
```

`src/CMakeLists.txt`:
```cmake
if(AVOX_ENABLE_ANALYSIS AND AVOX_ENABLE_ONNX)
    message(STATUS "avox_analysis YOLO-pose detection 可用")
    add_sub_path(avox_analysis AVOX_HEADER AVOX_SOURCE)
endif()
```

## 与现有 YOLODetector (inpaint) 的区别

| 项 | 现有 YOLODetector (inpaint) | 新 YOLODetector (analysis) |
|----|---------------------------|--------------------------|
| 用途 | 水印检测 | 人体检测 + 姿态估计 |
| 模型 | YOLO-detect / YOLO-seg | YOLO-pose |
| 类别数 | 4 (水印类型) | 1 (person) |
| 输出 | bbox / bbox+mask | bbox + 17关键点 + 动作标签 |
| 输入 | `uint8_t*` + width/height | `IImageBuffer*` |
| 预处理 | 直接 resize | Letterbox 保持宽高比 |
| 事件检测 | 无 | 人数变化 + 动作变化 |

## 性能优化（后续）

1. **模型量化**: INT8 量化减少模型大小和推理时间
2. **跳帧检测**: 隔 N 帧检测一次
3. **异步推理**: 独立推理线程，不阻塞帧队列

## 待确认事项

1. **模型文件**: `yolo26n-pose.onnx` 需要导出并放到 `assets/models/`
2. **动作分类精度**: 单帧关键点判断站立/坐下/躺下的可靠性需要实测，阈值可能需要根据实际场景调整
3. **多人匹配**: `detectEvents` 中前后帧人物匹配策略（当前按 bbox 位置就近匹配），复杂场景可能需要匈牙利算法
4. **Walking 动作**: 当前不做，后续可结合帧间关键点位移来判断