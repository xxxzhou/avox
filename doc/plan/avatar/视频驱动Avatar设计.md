# 视频驱动 Avatar 设计（ONNX 路线）

> 状态：设计稿 / 未实现
> 关联：`avox_avatar`(音频→ARKit52)、`avox_onnx`(IONNXSession)、`avox_vulkan`(ISurfaceRender/VkVideoRender)
> 前置阅读：`doc/plan/yolo26图像检测.md`（IImageBuffer→ONNX 喂推理范式）、`src/avox/AvoxAudio.h`（IAudioFace，本设计的镜像模板）

## 1. 背景与目标

从 `MediaPlayer`(媒体源) / `SourcePlayer`(相机) 拿到**处理后的视频帧**，提取**脸部**与**骨骼动画**，驱动到虚拟形象 `avox_avatar`。

- **脸**：帧 → 人脸关键点 + **ARKit 52 blendshape** → 喂现有 FaceNode / 未来 VRM。
- **身体**：帧 → 33 个人体关键点 → retargeting → avatar 骨骼。

选型结论（详见 §3）：**不引入 MediaPipe 官方 C++ 框架**，而是把 MediaPipe 的 face/pose landmarker 模型转成 ONNX，跑在已有的 `avox_onnx` 上。脸部输出 52 blendshape 天然 ARKit 兼容；身体需自建 retargeting 层。

## 2. 现状基线（确认过的）

### 已有
- **音频→口型链路**（`avox_avatar` 插件）：`IAudioFace`(`src/avox/AvoxAudio.h:211`) → `AudioFace`(`src/avox/audio/AudioFace.hpp:14`) → `Wav2ArkitFace`(`plugins/avox_avatar/`)，继承 `OnnxModelUser`，经 `OnnxSessionCache` 全局复用 session，输出 **ARKit 52 blendshape**，经 `IAudioFaceOb::onFaceBlendshape(AvoxData raw52, pts, final)`(`AvoxAudio.h:199`) 回调。
- **ONNX 推理抽象**：`IONNXSession::runShaped({{name, float*, shape}}, outNames, outputs)`(`src/avox/vision/IONNXSession.hpp:54`)；`OnnxSessionCache::acquire(OnnxModel, useGPU, deviceId, numThreads)`(`OnnxSessionCache.hpp:33`)；`OnnxModel` enum + `onnxModelPath()`(`OnnxModel.hpp`/`.cpp`)；`OnnxModelUser::session(path, useGPU)` 便捷入口。
- **图像喂推理范式**：`YoloDetector::bufferToBgr`(`plugins/avox_cv/yolo/YoloDetector.cpp:13`) IImageBuffer→cv::Mat；`letterbox/exactResize`→NCHW float /255→`runShaped`（`:258-300`）。可直接复用。
- **GPU 渲染管线 + 回读**：`VkVideoRender::vaildAndInitGraph`(`src/avox_vulkan/vulkan/VkVideoRender.cpp:226`)；`VkOutputLayer` 已具备 GPU→CPU 回读（`onCommand` 录 `imageToBuffer`，`onFrame` 零拷贝填 `cpuBuffer`）；`VkResizeLayer` GPU 缩放。
- **模型分发**：`assets/script/assets_manifest.json` + `fetch_assets.py`（`avatar_wav2arkit` 条目为模板，`:162-174`）。

### 缺失（本设计要补的）
- 视频帧 → 人脸关键点 / 人体姿态检测（无 MediaPipe/OpenPose/InsightFace）。
- ISurfaceRender 对外暴露"每帧处理后的 IImageBuffer"（目前只有 `enableYuvOut` 出 YUV）。
- 视频驱动 avatar 的接口（项目已规划：`AvoxAudio.h:186` 注释"后续非音频 avatar 接口(骨骼/表情驱动)另建 AvoxAvatar.h"）。
- 身体 retargeting / VRM 渲染（`main.gd` 多处"留 VRM"）。

## 3. 方案选型

| 方案 | 脸 | 身体 | 集成(本项目) | 跨平台 | 复用现有基建 |
|---|---|---|---|---|---|
| A. MediaPipe 官方 C++ | 478点+52bs | 33点 | 🔴 Bazel/无预编译/Windows 受限 | ❌ C++ 框架仅 Linux | ❌ |
| **B. MediaPipe 模型转 ONNX 跑 avox_onnx** ✅ | 同 A | 同 A | 🟡 转模型+帧管线 | ✅ avox_onnx 全平台 | ✅ OnnxSessionCache/IImageBuffer 全复用 |
| C. avox_cv 塞 YOLO-face | 🔴 弱(5点) | ❌ | 🟢 | ✅ | ✅ |
| D. 平台原生(ARKit/MLKit) | 🟢 最强 | 🟢 | 🟡 每平台一套 | ❌ 仅 iOS/Android | ❌ |

**选 B**：基建全复用、绕开 MediaPipe 构建地狱、脸部输出 ARKit52 天然对齐。脸 vs 身体难度差异：脸≈天（blendshape 直接用），身体=周（retargeting 是全新工作）。

## 4. 总体架构

```
MediaPlayer / SourcePlayer
        │  render(frame) 进入 VkVideoRender
        ▼
┌───────────────────────────────────────────────────────────┐
│ VkVideoRender 管线 (vaildAndInitGraph)                     │
│  inputLayer → yuv2RGBA → [effects...] → outNode            │
│      outNode ──► outputLayer (窗口显示)                    │
│      outNode ──► rgba2YUV → yuvOutLayer  (已有 enableYuvOut)│
│      outNode ──► imageResizeLayer → imageOutLayer (新: enableImage)
│                        │ GPU 缩到 landmarker 尺寸(192/256)
│                        ▼ 零拷贝回读(cpuBuffer=pinned staging)
└───────────────────────┬───────────────────────────────────┘
                        │ IImageBuffer (rgba8, 已缩好)
                        │  onRender 回调里安全读取
                        ▼
┌───────────────────────────────────────────────────────────┐
│ avox_avface 插件 (新, 镜像 avox_avatar)                       │
│  IAVFace::feed(IImageBuffer*) → preprocess(rgb/normalize)  │
│      → IONNXSession::runShaped (MediaPipe face/pose onnx)  │
│      → 52 blendshape (脸) / 33 landmarks (身体)            │
│      → IAVFaceOb::onFaceBlendshape / onPoseLandmarks 回调   │
└───────────────────────┬───────────────────────────────────┘
                        ▼
        FaceNode / VRM (脸: 复用现有 ARKit52 消费; 身体: retargeting 后驱动骨骼)
```

三块解耦、可独立落地：**帧管线(Phase 1)** → **ONNX 推理(Phase 2)** → **消费侧(Phase 3/4)**。

---

## Phase 1 — 帧输出管线 `enableImage`

### 1.1 目标
在 `ISurfaceRender` 加 `enableImage(IImageBuffer*)` / `disableImage()`：每帧把**处理后的帧**按 buf 的 `ImageFormat`（尺寸+格式）经 GPU 缩放后**零拷贝**回读到该 buf，用户在 `onRender`/`onFrame` 回调里安全读取，喂给 landmarker。

### 1.2 接口（草案）
```cpp
// src/avox/AvoxLayer.h —— ISurfaceRender 内, 与 enableYuvOut 并列
// 开启: 每帧把处理后的帧按 buf 的 ImageFormat(width/height/imageType) 缩放后零拷贝写入 buf
// buf 由调用方持有并保证生命周期到 disableImage 之后
// 在 ISurfaceRenderOb::onRender / onFrame 回调里读取 buf->getPointer() (同帧安全)
// imageType 当前支持: rgba8 / r8 (VkResizeLayer 稳定支持集)
virtual void enableImage(IImageBuffer* buf) {}
virtual void disableImage() {}
```
默认空实现，`SurfaceRenderNative` 不受影响（与 `enableYuvOut` 同款）。

### 1.3 实现路径（一路转发，镜像 enableYuvOut）
| 层 | 文件 | 改动 |
|---|---|---|
| 接口 | `src/avox/AvoxLayer.h:319` 旁 | 加 `enableImage/disableImage` 虚函数 |
| 基类 | `src/avox/video/VideoRender.hpp:46` 旁 | 加 `bool bEnableImage` / `ImageFormat imageOutFormat` / `enableImage/disableImage`（仿 `bOutCpuYuv`/`enableYuvOut`） |
| Vk 实现 | `src/avox_vulkan/vulkan/VkVideoRender.{hpp,cpp}` | 加 `imageResizeLayer`(VkResizeLayer) + `imageOutLayer`(VkOutputLayer) 成员；`enableImage/disableImage` 设标志 + `bResetFlag=true` |
| 管线分支 | `VkVideoRender.cpp:359-371` 旁 | 平行于 `rgba2YUV→yuvOutLayer`，加 `outNode→imageResizeLayer→imageOutLayer` 分支 |
| 转发 | `src/avox/video/SurfaceRenderVk.{hpp,cpp}:52` 旁 | `enableImage`→`vkVideoRender->enableImage` |

### 1.4 管线分支（核心）
在 `vaildAndInitGraph` 里，与现有 YUV 分支同款，off `outNode`（主 RGBA 处理链末端）拉一条分支：
```cpp
// 草案: 与 rgba2YUV 分支(359-371)平行
if (bEnableImage) {
  ImageType it = imageOutFormat.imageType;        // rgba8 / r8
  imageResizeLayer = graph->addNode<VkResizeLayer>(it);
  ReSizeParamet rp = {1, imageOutFormat.width, imageOutFormat.height};  // {bLinear,w,h}
  imageResizeLayer->get()->updateParamet(rp);
  imageOutLayer = graph->addNode<VkOutputLayer>();
  imageOutLayer->get()->updateParamet({true, false});   // {bCpu=true, bGpu=false}
  imageResizeLayer->get()->addLine(imageOutLayer);      // resize → outLayer
  outNode->addLine(imageResizeLayer);                   // outNode → resize (分支, 主链仍→outputLayer)
}
```
`bResetFlag` 触发 `resetGraph` 重录 cmd（**注意 memory: VkPipeGraph cmd 只录一次，改分支必须 resetGraph 重录**）。

### 1.5 零拷贝与同步（已验证）
- **零拷贝成立**：`VkOutputLayer::onFrame`(`VkOutputLayer.cpp:93-97`) `outBuffer->flush(true)` 后 `cpuBuffer->setData(outBuffer->getCpuData(), patchFormat, false)`，`false`=**引用**（`IImageBuffer` 注释 `AvoxVideo.h:153`）。`cpuBuffer.getPointer()` 直接是 staging VkBuffer 的持久映射内存（`VkWrapBuffer.cpp:85` initResoure 时 `vkMapMemory` 不 unmap）。
- **`flush(true)` 不是 memcpy**：`VkWrapBuffer.cpp:146` = `vkInvalidateMappedMemoryRanges`（HOST_CACHED 缓存失效），让 CPU 读到 GPU 最新写。
- **同帧安全**：`VkPipeGraph::onRun`(`VkPipeGraph.cpp:216`) 提交后 `vkWaitForFences(...UINT64_MAX)` 阻塞等整帧 GPU 完成（含回读拷贝）才调 `onFrame`。故 `graph->run()` 返回 → `onRenderOut` → 用户 `onRender` 里读 buf = **同帧、无撕裂、无延迟**（electron CPU 渲染路径同理）。复用同一 fence wait，**不新增同步开销**。

### 1.6 交付方式（pin 到 staging）
- **零拷贝引用（推荐）**：每帧 `buf->copyFrom(imageOutLayer 的 cpuBuffer, false)` 让 buf 引用 staging。`onRender` 里 `buf->getPointer()` 直读 GPU 映射内存。
  - 代价：buf 生命周期不能超 renderer；`disableImage` 后 buf 指针悬空（约定 disable 后不再读）。
  - 实现细节：`imageOutLayer` 的 `onCpuData(cpuBuffer)` 回调到 `VkVideoRender`；需与 YUV 分支的 `onCpuData` 区分（用独立 observer 或在 `VkOutputLayer` 加 `setOutputBuffer(IImageBuffer*)` 直接写，绕过 observer 歧义）。
- **拷贝（语义最干净）**：每帧 memcpy staging→buf（192²×4≈148KB，相对 ONNX preprocess 可忽略）。
- 性能大头是 **GPU 缩图后回读**（8MB→148KB），交付那一下拷不拷影响很小；对 landmarker 喂帧两者实测差异听不见。

### 1.7 格式约束
`VkResizeLayer`(`VkResizeLayer.cpp:12-22`) 稳定 shader：`rgba8`(默认 resize.comp)/`r8`(resizeC1)/`rgba32f`(resizeF4)/`r16`。face/pose landmarker 喂 **rgba8**；纯灰度检测用 **r8**。`rgb8`/planar 先不支持（需加转换层，后续）。

---

## Phase 2 — ONNX MediaPipe 推理插件

### 2.1 模型
| 模型 | 输入 | 输出 | 备注 |
|---|---|---|---|
| Face Landmarker | `[1,192,192,3]` RGB, norm [-1,1] | 478 landmarks `[1,478,3]` + **52 blendshape** `[1,52]` + 可选 transform `[1,4,4]` | 52 blendshape **= ARKit 兼容**，直接复用 |
| Pose Landmarker | `[1,256,256,3]` RGB, norm [-1,1] | 33 landmarks(2D+3D) `[1,33,5]` + 33 world landmarks | 身体用 |

### 2.2 模型获取与转换
- 官方分发是 `.task`（TFLite bundle：detector + facemesh V2 + blendshape 回归头）。
- 转换路径：解包 `.task`(zip) → `.tflite` → `python -m tf2onnx.convert --tflite X.tflite --output X.onnx`。
- **关键验证**：blendshape 回归子模型经转换是否干净保留。兜底：从 478 landmarks 用 MediaPipe `face_geometry` 系数**确定性推算** 52 blendshape（无需模型）。
- 社区现成 ONNX 移植：`yakhyo/mediapipe-face-mesh-onnx`、`PINTO0309`、Qualcomm HF（部分无 blendshape 头，需补）。
- 放 `assets/models/mediapipe/`，登记进 `assets_manifest.json`（仿 `avatar_wav2arkit` 条目）。

### 2.3 模型注册（两处）
1. `OnnxModel` enum 加值（`OnnxModel.hpp:14`）：
   ```cpp
   MediaPipeFace,   // face landmarker: 192²→478lm+52bs
   MediaPipePose,   // pose landmarker: 256²→33lm
   ```
2. `onnxModelPath` 加分支（`OnnxModel.cpp:7`）：
   ```cpp
   case OnnxModel::MediaPipeFace: return getModelFilePath("mediapipe/face_landmarker.onnx");
   case OnnxModel::MediaPipePose: return getModelFilePath("mediapipe/pose_landmarker.onnx");
   ```
   > 注：`onnxModelPath` 改了，用到它的插件要重编（memory: 核心改 OnnxModel 影响插件 ABI 偏移，但 enum 加值通常安全；hub 成员加须末尾追加）。

### 2.4 新插件 `plugins/avox_avface`（镜像 `avox_avatar`）
目录骨架（仿 `avox_avatar`）：
```
plugins/avox_avface/
  CMakeLists.txt          register_plugin(avox_avface DYNAMIC), AVOX_ENABLE_AVFACE
  AvFaceModule.{hpp,cpp}  loadModule: audioFaceHub/新 hub.reg("mediapipe", []{return new MediaPipeFace;})
  MediaPipeFace.{hpp,cpp} 继承 AVFace(基类) + RunTask + OnnxModelUser
```
- 经 `OnnxModelUser`/`OnnxSessionCache::acquire(OnnxModel::MediaPipeFace, useGPU=false, dev, threads)` 复用 session（wav2arkit 同款，模型常驻）。
- `useGPU=false`（Intel UHD 770 集显，CPU 推理更稳；后续可开 EP)。
- worker 线程取帧推理（`RunTask`），避免阻塞渲染线程。

### 2.5 推理 preprocess（复用 YoloDetector 范式）
```cpp
// 输入 buf = enableImage 出来的 rgba8 IImageBuffer(已 GPU 缩到 192/256)
// 1. rgba8 → rgb8 strip（或 r8）
// 2. /127.5 - 1.0  归一化到 [-1,1]（MediaPipe 约定，转换后核对）
// 3. HWC → CHW float
// 4. runShaped({{inName, chw.data(), {1,3,192,192}}}, outNames, outputs)
std::vector<std::string> outs = {"face_landmarks", "blendshapes"};  // 转换后核对实际名
session->runShaped({{inName, input.data(), shape}}, outs, outputs);
// outputs[1] = 52 blendshape (ARKit 序); 直接当 ARKit52 输出
```
- 实际 in/out 名以转换后 ONNX 为准（用 `getInputNames/getOutputNames` 取，`YoloDetector::ensureLoaded:188` 同款）。
- 性能：192²×3 单脸 CPU 推理 ~10-20ms/帧，30fps 摄像头够用；超时丢帧保实时。

---

## Phase 3 — 消费侧接口（脸，复用 ARKit52）

### 3.1 新建 `src/avox/AvoxAvatar.h`（项目已规划）
镜像 `IAudioFace`(`AvoxAudio.h:211`)，把"音频驱动"换成"视频驱动"，输出**同样的 52 blendshape**：
```cpp
// src/avox/AvoxAvatar.h (新)
struct AVFaceDesc { int32_t fps = 30; int32_t blendshapeCount = 52; };
class IAVFaceOb {
 public:
  virtual void onFaceDesc(const AVFaceDesc& desc) {}
  virtual void onFaceBlendshape(const AvoxData& raw52, int64_t pts) {}  // 与 IAudioFaceOb 同形
  virtual void onFaceError(const char* err) {}
};
class IAVFace {
 public:
  virtual void start() = 0;
  virtual void feed(IImageBuffer* frame, int64_t pts) = 0;   // ← 视频帧入
  virtual void stop() = 0;
  virtual bool loading() = 0;
};
```
基类 `AVFace`（`src/avox/avatar/AVFace.hpp`，仿 `AudioFace`）：组合 `IAVFace` + `Observer<IAVFaceOb>`。

### 3.2 复用现有 FaceNode 消费
- 现有 `FaceNode`(`platform/godot/plugin/src/face.cpp`) 消费 `IAudioFaceOb::onFaceBlendshape`。视频 blendshape 走 `IAVFaceOb::onFaceBlendshape`（**同形 52 float**），FaceNode 加一路订阅即可，消费侧零改动。
- 顺带补 `ArkitBlendshape` 命名枚举（现仅 `kArkitBlendshapeCount=52` + 硬编码 24/33，技术债）。

### 3.3 帧如何喂进 IAVFace（接线）
```
SourcePlayer.onVideoFrame / ISurfaceRenderOb.onRender
  → render.enableImage(buf) 后, onRender 里拿 buf
  → avFace->feed(buf, pts)   // avox_avface worker 线程推理
  → onFaceBlendshape(52) → FaceNode/VRM
```

---

## Phase 4 — 身体骨骼 + VRM（难点，后续）

- **pose landmarker**：33 个 2D+3D 关键点 → 需 **retargeting/IK**（关节朝向、rest-pose/T-pose 对齐）才是全新工作。参考 DollarsMocap/SO 的 landmark→bone rotation 方法。
- **VRM**（glTF 扩展，原生支持 ARKit blendshape + humanoid 骨骼）：替换 `main.gd` 的 2D 简笔占位。Godot 经 glTF 加载 VRM。
- Godot 加 `AvatarNode`(3D)，统一收 blendshape + pose 两路驱动。
- **此阶段工作量大（周级），先不做**；Phase 1-3 先把脸部打通。

---

## 5. 接口签名汇总（草案）

| 位置 | 签名 |
|---|---|
| `AvoxLayer.h` ISurfaceRender | `void enableImage(IImageBuffer* buf)` / `void disableImage()` |
| `VideoRender.hpp` 基类 | `void enableImage(IImageBuffer*)` / `void disableImage()` / `bool bEnableImage` / `ImageFormat imageOutFormat` |
| `AvoxAvatar.h`(新) | `IAVFace`(`start/feed(IImageBuffer*,pts)/stop/loading`) + `IAVFaceOb`(`onFaceBlendshape`) |
| `OnnxModel.hpp`(新值) | `MediaPipeFace` / `MediaPipePose` |
| C 接口 `*Export.h` | `createAVFace(type)` / 销毁（仿 `createAudioFace`） |
| SWIG/Godot | `AVFaceNode` 或 FaceNode 扩视频源（仿 `face.cpp`） |

## 6. 落地步骤（文件级 checklist）

### Phase 1（帧管线，可独立交付）
- [x] `AvoxLayer.h`：ISurfaceRender 加 `enableImage/disableImage` 虚函数
- [x] `VideoRender.{hpp,cpp}`：加 `bEnableImage/imageOutFormat/enableImage/disableImage`（仿 enableYuvOut）
- [x] `VkVideoRender.{hpp,cpp}`：加 `imageResizeLayer/imageOutLayer` 成员 + `enableImage/disableImage` + `vaildAndInitGraph` 分支
- [x] `VkOutputLayer`：加 `setOutputBuffer`（onFrame 零拷贝 `copyFrom(cpuBuffer,false)` 直投用户 buf，绕过 observer 歧义）
- [x] `SurfaceRenderVk.{hpp,cpp}`：转发
- [x] sample：`samples/vulkantest/enableimagetest.cpp`（MediaPlayer + enableImage + onRender）已实测通过：
      192×192 rgba8 缩图、`bDataRef:1` 零拷贝、rowPitch 768/bufSize 147456、~24fps 同帧安全、存盘 PNG 真实画面

### Phase 2（ONNX 推理）
- [x] 模型获取（两段式，统一用 Google 官方 `.task` bundle 经 takoyakisoft VisionOnnxExporter 导出的 ONNX，Apache-2.0；HF `takoyakisoft/vtuberkit-vision-landmarkers-onnx`）：
      ① `face_landmarker.onnx`（4.69MB, 官方导出）`input_12[B,256,256,3] NHWC -> Identity[B,1,1,1434]=478x3 landmark-major + presence + scalar`
      ② `face_blendshape.onnx`（1.80MB, Google 官方 blendshape 头本尊，输出名 `StatefulPartitionedCall:0`=TF 原生签名）`serving_default_input_points:0[1,146,2] batch固定1 -> [52] 已sigmoid` ARKit
      两模型均 onnxruntime 实测通过（SHA/字节与 manifest 完全吻合），已落 `assets/models/avatar/`。
      注：早期试过 yakhyo landmarker + py-feat blendshape 近似，已废弃换官方版（消除跨源归一化风险）；`script/convert_mp_blendshape_onnx.py` 已删。
- [x] `assets_manifest.json`：加 `avatar_mediapipe_landmarker` + `avatar_mediapipe_blendshape`（均 download, takoyakisoft HF resolve, sha/size 实测填）
- [x] `OnnxModel.{hpp,cpp}`：加 `MediaPipeFaceLandmarker` / `MediaPipeBlendshape` enum + onnxModelPath
- [ ] `MediaPipeFace` 插件（放 `plugins/avox_avatar/`，仿 `Wav2ArkitFace` 三明治，非新 avox_avface）
- [ ] `AvoxAvatar.h`：IAVFace/IAVFaceOb/AVFace 基类（镜像 IAudioFace）
- [ ] preprocess（rgba8→中心裁剪→256→**NHWC** RGB norm[-1,1]）+ 478→146 子集（**归一化！官方头吃归一化 landmark 非原始像素，须真脸校正**）+ 两段 run（blendshape 逐帧 batch=1）+ 输出 52 blendshape
      （MVP 中心裁剪：avatar 驱动场景人脸居中；稳健性后续接 BlazeFace 检测器，见同 repo `face_detector.onnx` = face_detection_short_range 128×128）

### Phase 3（消费）
- [ ] `createAVFace` C 接口 + `AvoxManager` hub 注册（**末尾追加**，memory: ABI 偏移）
- [ ] Godot `face.cpp`：FaceNode 扩视频 blendshape 订阅（或新 AVFaceNode）
- [ ] `ArkitBlendshape` 命名枚举
- [ ] 端到端：相机 → enableImage → avFace.feed → onFaceBlendshape → FaceNode 驱动全脸（非仅 24/33）

### Phase 4（身体/VRM，后续）
- [ ] pose landmarker 推理 + 33 点输出
- [ ] retargeting/IK 层
- [ ] VRM 加载 + AvatarNode

## 7. 风险与坑
- **blendshape 头转换**：`.task→onnx` 时 52 blendshape 回归头可能不干净保留；兜底用 478 landmark 确定性推算。**先验证再大规模集成。**
- **GPU 帧喂不了 CV**：CV/ONNX 强制 CPU `IImageBuffer`，VkImage→CV 无自动管线。Phase 1 的 `enableImage` 正是补这条（GPU 缩图后零拷贝回读 CPU）。
- **CPU 推理带宽**：开发机 Intel UHD 770 集显，本地 AI 带宽瓶颈。务必 GPU 先缩图（192/256）再回读再推理；30fps 摄像头超时丢帧保实时。
- **VkPipeGraph cmd 只录一次**（memory）：改 enableImage 分支必须 `bResetFlag`→resetGraph 重录，否则数据不流。
- **AvoxManager hub 末尾追加**（memory）：新 `avFaceHub` 必须加在 `AvoxManager.hpp` 末尾，插中间破坏插件 ABI 偏移致 ensureStarted 崩。
- **OnnxModel/onnxModelPath 改动**：核心改，用到它的插件(avox_avatar/avox_cv/avox_translation/avox_ocr)须重编。
- **跨平台格式**：`rgba8` 优先；VkResizeLayer 不直接支持 rgb8/planar（需转换层）。
- **retargeting 是真活**（Phase 4）：脸=天，身体=周，先打通脸部。
- **CMake GLOB 无 CONFIGURE_DEPENDS**（memory）：新增 `plugins/avox_avface/*.cpp` 后删 `CMakeCache.txt` 强制 re-configure，否则不编进/符号未解析。

## 8. 参考资料
- MediaPipe Face Landmarker（478 点 + 52 ARKit blendshape）：https://developers.google.com/edge/mediapipe/solutions/vision/face_landmarker
- MediaPipe Pose Landmarker（33 点）：https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker
- **官方 ONNX 源（本项目所用）**：takoyakisoft/vtuberkit-vision-landmarkers-onnx — Google `.task` 经 VisionOnnxExporter 导出的 face_landmarker/face_blendshape/face_detector(BlazeFace)/pose/hand 全套，含 manifest+metadata：https://huggingface.co/takoyakisoft/vtuberkit-vision-landmarkers-onnx
- MediaPipe Face Mesh ONNX 移植（C++ 参考，未采用）：https://github.com/yakhyo/mediapipe-face-mesh-onnx
- tf2onnx（.task→.onnx）：https://github.com/onnx/tensorflow-onnx
- ONNX Runtime vs MediaPipe 2025 对比：https://medium.com/@sharmapraveen91/onnx-runtime-mobile-vs-mediapipe-your-2025-guide-to-building-ai-powered-mobile-apps-7c49a222b879
- MediaPipe 关键点→3D avatar 骨骼（retargeting 参考）：https://www.dollarsmocap.com/blog/mediapipe-to-3d-avatar
