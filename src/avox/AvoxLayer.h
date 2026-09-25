#pragma once

#include "AvoxDef.h"
#include "AvoxMath.h"
#include "AvoxVideo.h"

namespace avox {

#define AVOX_MAP_RENDER_TYPE(type) \
  XX(other, 0, "other")           \
  XX(Vulkan, 1, "vulkan")         \
  XX(D3D11, 2, "d3d11")           \
  XX(D3D12, 3, "d3d12")           \
  XX(OpenGLES, 4, "opengles")     \
  XX(Metal, 5, "metal")

enum class RenderType {
#define XX(name, value, str) name = value,
  AVOX_MAP_RENDER_TYPE(XX)
#undef XX
};

enum class GpuType : int32_t {
  // 无GPU,默认对应CPU版本管线
  noGpu = 0,
  cuda,
  vulkan,
};

// 节点插槽
struct NodeSlot {
  int32_t index = -1;
  int32_t slot = 0;
  inline bool operator==(const NodeSlot& right) const {
    return index == right.index && slot == right.slot;
  }
  inline bool valid() { return index >= 0 && slot >= 0; }
  inline void operator=(const NodeSlot& right) {
    index = right.index;
    slot = right.slot;
  }
};

// 节点连线
struct NodeLine {
  NodeSlot from = {};
  NodeSlot to = {};

  inline bool valid() { return from.valid() && to.valid(); }
  inline bool operator==(const NodeLine& right) const {
    return from == right.from && to == right.to;
  }

  inline void operator=(const NodeLine& right) {
    from = right.from;
    to = right.to;
  }
};

class IPipeGraph;

// 节点接口
class INode {
 public:
  INode() = default;
  virtual ~INode() = default;

 public:
  // 获取图
  virtual IPipeGraph* getPipeGraph() = 0;
  // 获取节点索引
  virtual int32_t getNodeIndex() = 0;
  // 输入节点插槽数量
  virtual int32_t inSlotCount() = 0;
  // 输出节点插槽数量
  virtual int32_t outSlotCount() = 0;
  // 是否输入层
  virtual bool bInputNode() = 0;
  // 是否输出层
  virtual bool bOutputNode() = 0;
};

// PipeGraph节点接口
class IPipeNode : public INode {
 public:
  IPipeNode() = default;
  virtual ~IPipeNode() = default;

 public:
  // a->b
  virtual IPipeNode* addLine(IPipeNode* b, int32_t asite = 0,
                             int32_t bsite = 0) = 0;
  // 是否群集节点
  virtual bool bGroup() { return false; }
  // 返回节点里放入的内容
  virtual INode* getContent() { return nullptr; }
};

class IPipeGraph {
 public:
  IPipeGraph() = default;
  virtual ~IPipeGraph() = default;

 public:
  virtual IPipeNode* getNode(int32_t index) = 0;
  virtual IPipeNode* addLine(IPipeNode* a, IPipeNode* b, int32_t inSite = 0,
                             int32_t bsite = 0) = 0;

  // 清除连线，保留节点
  virtual void clearLines() = 0;
  // 清除所有节点与线
  virtual void clear() = 0;
  // 执行
  virtual void run() = 0;
  // 重启
  virtual void reset() = 0;
};

// 类似D3D11/D3D12/OpenGLES/Vulkan的Context
class IRenderContext {
 public:
  IRenderContext() = default;
  virtual ~IRenderContext() = default;

 public:
  virtual RenderType getRenderType() = 0;
};

class IVInputLayerOb {
 public:
  IVInputLayerOb() = default;
  virtual ~IVInputLayerOb() = default;

 public:
  virtual void onImageChange(const ImageFormat& format) = 0;
};

// 输入图像节点
class IVInputLayer {
 public:
  IVInputLayer() = default;
  virtual ~IVInputLayer() = default;

 public:
  virtual void inputCpuData(IImageBuffer* buffer, bool bCopyData) = 0;
  virtual void inputCpuData(const YUVFrame& frame, bool bCopyData) {};
  virtual void inputGpuData(IRenderContext* context) {};
};

struct OutputParamet {
  int32_t bCpu = true;
  int32_t bGpu = false;
};

class IVOutputLayerOb {
 public:
  IVOutputLayerOb() = default;
  virtual ~IVOutputLayerOb() = default;

 public:
  virtual void onImageChange(const ImageFormat& format) {};
  virtual void onCpuData(IImageBuffer* buffer) {};
  virtual void onGpuProcess() {};
  virtual void onGpuData(IRenderContext* context) {};
};

class IVOutputLayer {
 public:
  IVOutputLayer() = default;
  virtual ~IVOutputLayer() = default;

 public:
  virtual void outputGpuData(IRenderContext* context) {};
  virtual bool fetchData(IImageBuffer* buffer) { return false; };
};

struct Watermark {
  // 水印位置
  float centerX = 0.8f;
  float centerY = 0.2f;
  // 水印宽高
  float width = 0.2f;
  float height = 0.2f;
  // 水印不透明度
  float alaph = 0.8f;
};

struct LutParamet {
  // 1 amatorka 2 miss etikate
  int32_t lutIndex = 0;
};

// 水平定位
enum class HAlignType { none = 0, left, right, mid };
// 垂直定位
enum class VAlignType { none = 0, top, bottom, mid };

// 定位
struct Alignment {
  // 水平
  HAlignType horizontal = HAlignType::none;
  // 垂直
  VAlignType vertical = VAlignType::none;
};

// 锐度参数 (区别于 VkExport.h 中的 SharpenParamet)
struct SharpenVideo {
  int32_t offset = 1;      // 采样偏移
  float sharpness = 0.0f;  // 清晰度调整 (-4.0 ~ 4.0, 默认 0.0)
};

// 基础图像调整: 色调/亮度/对比度/饱和度/伽玛 合并到单 shader 单 pass
// 各字段均有中性默认值; enableBasicAdjust 一次传入整组, 只改需要的字段即可
// 字段顺序必须与 basicAdjust.comp 的 UBO 成员顺序一致(宏按 struct 原样上传)
struct BasicAdjustParamet {
  float hue = 0.0f;         // 色调(度), -360~360, 0=不变
  float brightness = 0.0f;  // 亮度, -1~1, 0=不变
  float contrast = 1.0f;    // 对比度, 0~2, 1=不变
  float saturation = 1.0f;  // 饱和度, 0~2, 1=不变
  float gamma = 1.0f;       // 伽玛, 0~3, 1=不变
  inline bool operator==(const BasicAdjustParamet& right) const {
    return this->hue == right.hue && this->brightness == right.brightness &&
           this->contrast == right.contrast &&
           this->saturation == right.saturation && this->gamma == right.gamma;
  }
};

// Anime4K 预设模式
enum class Anime4KMode : int32_t {
  ModeA = 0,  // Restore + Upscale, 适合高压缩率动漫(模糊明显)
  ModeB = 1,  // Restore_Soft + Upscale, 适合较清晰动漫(避免过度锐化)
  ModeC = 2,  // Upscale only, 适合高质量原片(仅需放大)
};
// CNN 模型变体, 越大画质越好但越慢
enum class Anime4KVariant : int32_t {
  S = 0,  // Small, ~2ms, 性能优先
  M = 1,  // Medium, ~5ms, 画质与性能平衡
  L = 2,  // Large, ~10ms, 画质优先
};
struct Anime4KParamet {
  Anime4KMode mode = Anime4KMode::ModeA;       // 预设模式
  Anime4KVariant variant = Anime4KVariant::M;  // CNN模型变体
  float strength = 1.0f;                       // Restore强度(0~1, 1为满强度)
  bool enableClampHighlights = true;           // 防止高光区域过曝
  bool operator==(const Anime4KParamet& r) const {
    return mode == r.mode && variant == r.variant && strength == r.strength &&
           enableClampHighlights == r.enableClampHighlights;
  }
  bool operator!=(const Anime4KParamet& r) const { return !(*this == r); }
};

// 画质增强模型选择
enum class QualityModel : int32_t {
  SpanX4 = 0,  // SPAN 4x (NomosUni-span-multijpg), 盲退化恢复, 比旧 x4v3 轻 2 倍级
};
// 画质增强输出模式
enum class QualityOutputMode : int32_t {
  Restore =
      0,  // 1x: x4 推理 → 缩放回原分辨率 (iGPU ~131ms 快, 但 infer 太小细节差)
  Upscale2x =
      1,  // 2x: x4 推理 → 缩放到 2x (默认; 效果好, iGPU ~540ms, 异步不卡渲染)
  Upscale4x = 2,  // 4x: x4 推理 → 直接输出 4x
  Auto = 3,       // ≥1080P → Upscale2x, <1080P → Upscale4x
};
struct QualityEnhanceParamet {
  QualityModel model = QualityModel::SpanX4;
  QualityOutputMode outputMode = QualityOutputMode::Upscale2x;
  int32_t skipFrames = 5;  // 抽帧间隔 (0=每帧都处理, N=每N帧处理一次, 默认5)
  bool operator==(const QualityEnhanceParamet& r) const {
    return model == r.model && outputMode == r.outputMode &&
           skipFrames == r.skipFrames;
  }
  bool operator!=(const QualityEnhanceParamet& r) const {
    return !(*this == r);
  }
};

// FSR实时增强放大倍数
enum class FSRScale : int32_t {
  Upscale2x = 0,  // 2x放大 (1080p→4K, 最常用)
  Upscale4x = 1,  // 4x放大 (540p→4K)
  Restore = 2,    // 不放大, 只做去块+锐化 (清晰视频推荐)
};
// FSR去块模式
enum class DeblockMode : int32_t {
  Bilateral = 0,  // 双边滤波 (保边去块, 边缘不变暗, 推荐)
  Guided = 1,     // 导向滤波自引导 (box blur均值混合, 边缘偏暗, 实验性)
};
// FSR实时增强参数 (与 Anime4K / QualityEnhance 互斥)
struct FSRParamet {
  FSRScale scale = FSRScale::Upscale2x;
  float deblockStrength =
      0.0f;  // 去块强度 (0=关, 0~1越大越平滑), 清晰视频建议0
  DeblockMode deblockMode =
      DeblockMode::Bilateral;  // 去块模式 (bilateral保边/导向滤波)
  bool enableRCAS = true;      // true=FSR-RCAS锐化, false=普通Sharpen
  float rcasSharpness =
      0.2f;  // RCAS锐化强度 (0=最大锐化, N>0越弱), Restore模式建议0.2
  bool enableLinear =
      true;  // 线性空间处理 (sRGB↔线性, 官方要求; 关闭可对比 gamma 空间)
  bool operator==(const FSRParamet& r) const {
    return scale == r.scale && deblockStrength == r.deblockStrength &&
           deblockMode == r.deblockMode && enableRCAS == r.enableRCAS &&
           rcasSharpness == r.rcasSharpness && enableLinear == r.enableLinear;
  }
  bool operator!=(const FSRParamet& r) const { return !(*this == r); }
};

// VR投影模式: 每眼画面到虚拟透视相机的重映射(声明序即 shader 分支依据)
enum class VrProjection : int32_t {
  fisheye180 = 0,  // 每眼180°鱼眼圆, 等距投影(VR180主流)
  fisheye360 = 1,  // 每眼360°全圆鱼眼(少见)
  equirect180 = 2, // 每眼180°经距等距柱状(VR180的equirect变体)
  equirect360 = 3, // 每眼360°全景等距柱状
};
// VR眼布局
enum class VrEyeLayout : int32_t { sbs = 0, ou = 1 };
// VR输出模式(setVrOutMode, 运行时可切不重建graph)
enum class VrOutMode : int32_t {
  mono = 0,       // 单眼透视画面(平面屏标准观看, 恒取左眼)
  anaglyph = 1,   // 红蓝3D(R取左眼, GB取右眼, 配红青眼镜)
  sbsPreview = 2, // 左右眼并排各按当前视角重映射(对比/外接预览)
};
// VR播放参数(enableVr): center/radius 全零时按默认取值
// (圆内切于每眼画幅高度、水平居中), 覆盖绝大多数片源; 个别镜头偏移的
// 片源由宿主填全帧归一化uv修正
struct VrParamet {
  VrProjection projection = VrProjection::fisheye180;
  VrEyeLayout eyeLayout = VrEyeLayout::sbs;
  float fisheyeFov = 180.0f;  // 鱼眼镜头fov(度), equirect忽略
  float centerL[2] = {0, 0};  // 左眼圆心, 全帧归一化uv; 全零=默认
  float centerR[2] = {0, 0};  // 右眼圆心, 同上
  float radiusL = 0.0f;       // 左眼圆半径, 帧高占比; 0=默认内切(0.5)
  float radiusR = 0.0f;       // 右眼圆半径, 同上
  inline bool operator==(const VrParamet& r) const {
    return projection == r.projection && eyeLayout == r.eyeLayout &&
           fisheyeFov == r.fisheyeFov && centerL[0] == r.centerL[0] &&
           centerL[1] == r.centerL[1] && centerR[0] == r.centerR[0] &&
           centerR[1] == r.centerR[1] && radiusL == r.radiusL &&
           radiusR == r.radiusR;
  }
  inline bool operator!=(const VrParamet& r) const { return !(*this == r); }
};

// 渲染输出事件: 每帧随 onRender 在渲染线程派发, 回调来了即有图
struct SurfaceRenderEvent {
  uint32_t structSize = sizeof(SurfaceRenderEvent);
  uint32_t rebuilt = 0;    // 1=相对上一帧输出图重建过(首帧/换片/分辨率变化/特效开关)
  uint32_t hdr = 0;        // 1=本帧HDR直通(PQ落帧, forceHDR); 元数据走onHdrMeta
  uint32_t nativeType = 0; // nativeTexture资源类型(0=无, 原生车道接入时定枚举)
  // 输出图世代: 导出资源重建时+1, 全局单调, 跨帧比较判断GPU资源是否需重建
  uint64_t generation = 0;
  // 本帧输出图格式(宽高/rowPitch/像素类型), 消费端按它建纹理, 勿硬编码RGBA8
  ImageFormat format = {};
  // 稳定标识句柄(非转移语义, avox持有; 未启用对应通道为0/null)
  uint64_t dx11Handle = 0;   // Windows: 共享纹理NT句柄(enableVkOutputDx11通道)
  uint64_t dx11Fence = 0;    // Windows: 共享fence NT句柄
  void* ioSurface = nullptr; // Apple: IOSurfaceRef(非所有权, 不得CFRelease)
  uint64_t ioSurfaceId = 0;  // Apple: IOSurfaceGetID(), 换面探测
  // 原生车道资源标识(HDR原生窗口对接预留, 本轮仅定义)
  void* nativeTexture = nullptr; // Windows原生车道: ID3D11Texture2D*
};

class ISurfaceRenderOb {
 public:
  ISurfaceRenderOb() = default;
  virtual ~ISurfaceRenderOb() = default;

 public:
  virtual void onSurface() {};
  virtual void onWinSizeChange(int32_t width, int32_t height) {};
  // 需要windowrender打开enableYuvOut,electron返回CPU数据给网页
  // buf恒为packed布局(ImageType/尺寸由yuvType经yuv2ImageFormat确定: 平面类r8/r16,
  // yuv420P/422P的UV物理行为[偶行|奇行|pad]; 交织类rgba8半宽),可直接喂GPU;
  // 需按stride等距逐行读(ffmpeg/CPU色转)时调image2SplitYUVFrame取split的YUVFrame。
  // buf生命周期仅限本回调内,要留用请自行拷贝
  virtual void onFrame(IImageBuffer* buf, YuvType yuvType) {};
  // 每帧渲染输出事件(渲染线程): 世代/格式/互操作句柄, 回调内禁止阻塞
  virtual void onRender(const SurfaceRenderEvent* ev) {};
};

// 设置渲染窗口
class ISurfaceRender {
 public:
  ISurfaceRender() = default;
  virtual ~ISurfaceRender() = default;

 public:
  // 默认bVulkan为true,会创建vulkan窗口
  // 否则创建平台对应原生的dx11/egl/metal窗口
  virtual void setVulkan(bool bVulkan) = 0;
  // window handle/android ANativeWindow/ios CAMetalLayer
  virtual void setSurface(void* surface) = 0;
  // 设置空窗口,给非窗口程序使用,如离屏渲染,electron CPU渲染
  // ytype有值时自动调用enableYuvOut打开YUV输出
  virtual void setOffSurface(YuvType ytype) = 0;
  virtual void* getSurface() = 0;
  // setSurface默认只输出到GPU到窗口,搭配setSurface使用
  virtual void enableYuvOut(YuvType ytype) = 0;
  virtual void disableYuvOut() = 0;
  // 设置源视频色彩空间(矩阵标准+量程), 决定 yuv2RGBA/rgba2YUV 转换, 运行时可调
  virtual void setColorSpace(const ColorSpaceDesc& cs) {};
  // 当前输出YUV帧实际编码所用的色彩空间: onFrame/screenShot 拿到 YUVFrame 后
  // 经 yuvframe2Rgba 转 RGBA 必须传同一 cs, 否则 limited/bt709 流快照会发灰
  virtual ColorSpaceDesc getOutColorSpace() { return ColorSpaceDesc{}; };
  // HDR输出模式(默认follow), 运行时可调。tone map 仅 Vulkan 车道实现:
  // follow=按流与元数据自动(HDR进 tone map 出SDR); forceSDR=恒tone map;
  // forceHDR=跳过tone map(PQ编码值原样落帧, SDR表面表现为过曝, 宿主自担)
  virtual void setHdrMode(HdrMode mode) {};
  // 调用方须先对 buf 调用 setImageFormat 设定 width/height/imageType(当前仅
  // rgba8); 每帧按该 ImageFormat 经 GPU 缩放后零拷贝写入 buf, 读取时以
  // buf->getImageFormat() 为准(rowPitch 可能对齐到 16 字节) buf 由调用方持有,
  // 生命周期需维持到 disableImage 之后 在 ISurfaceRenderOb::onRender/onFrame
  // 回调里读取 buf->getPointer() (同帧安全, graph->run 已 vkWaitForFences)
  virtual void enableImage(IImageBuffer* buf) {};
  virtual void disableImage() {};
  // 自适应长宽比,默认为true,为false则是全屏
  virtual void setAutoAspect(bool bEnable) = 0;
  // 抓取帧
  virtual bool screenShot(IImageBuffer* imageBuffer) { return false; };
  // 设置窗口大小
  virtual void enableSizeChange(int32_t width, int32_t height) {};
  virtual void enableSizeScale(float scale) {};
  virtual void disableSizeChange() {};
  // Anime4K
  virtual void enableAnime4K(const Anime4KParamet& paramet) {}
  virtual void disableAnime4K() {}
  // 画质增强 (Real-ESRGAN, 与 Anime4K/FSR 互斥)
  virtual void enableQualityEnhance(const QualityEnhanceParamet& paramet) {}
  virtual void disableQualityEnhance() {}
  // 实时增强 (FSR+双边, 与 Anime4K/QualityEnhance 互斥)
  virtual void enableFSR(const FSRParamet& paramet) {}
  virtual void disableFSR() {}
  // 水印
  virtual void enableWatermark(const Watermark& paramet,
                               IImageBuffer* imageBuffer) {};
  virtual void disableWatermark() {};
  // Lut
  virtual void enableLut(const LutParamet& paramet) {};
  virtual void disableLut() {};
  // 基础图像调整: enableBasicAdjust 传入整组参数(各字段默认中性),
  // disableBasicAdjust 关整组. 详见 BasicAdjustParamet
  virtual void enableBasicAdjust(const BasicAdjustParamet& paramet) {};
  virtual void disableBasicAdjust() {};
  // 锐度
  virtual void updateSharpen(const SharpenVideo& paramet) {};
  virtual void disableSharpen() {};
  // VR播放: 投影重映射+视角交互(仅Vulkan车道实现), 参数见 VrParamet
  virtual void enableVr(const VrParamet& paramet) {}
  virtual void disableVr() {}
  // 视角增量(度), 累加与钳位都在SDK内部(宿主勿自持视角状态):
  // fisheye180/equirect180 yaw/pitch各钳±90°, 360格式yaw取模包绕, 到边界即停
  virtual void rotateView(float deltaYaw, float deltaPitch) {}
  // 视野增量(度), 钳位[30,120], 负值=放大
  virtual void zoomView(float deltaFov) {}
  // 视角复位(yaw=0,pitch=0,fov=90)
  virtual void resetView() {}
  // 取当前视角(度), 供宿主UI显示/记忆
  virtual void getViewAngles(float* yaw, float* pitch, float* fov) {}
  // VR输出模式, 快速通道不重建graph
  virtual void setVrOutMode(VrOutMode mode) {}
  // 立体强度(度, 0~5): 双眼水平反向各偏转一半加大视差, 只影响
  // anaglyph/sbsPreview(mono单眼无效); 负值自动钳0, 快速通道
  virtual void setVrStereoStrength(float strengthDeg) {}
};

// 静态图像渲染器 - 显示单张图片,不用RunTask循环
// 持有Window + VkVideoRender,调用render()时触发单次渲染+呈现
// 窗口消息循环由调用者负责
// setSurface通过getSurfaceRender()->setSurface()调用
class IImageRender {
 public:
  IImageRender() = default;
  virtual ~IImageRender() = default;

 public:
  // 获取底层SurfaceRender,用于IFontLayer/IGeometryLayer等扩展
  // 也通过它调用setSurface/screenShot等ISurfaceRender方法
  virtual ISurfaceRender* getSurfaceRender() = 0;
  // 更新图像并渲染,支持rgba8/bgra8/argb8等格式
  virtual void render(IImageBuffer* imageBuffer) = 0;
  // 更新YUV帧并渲染,支持yuv420P/nv12/yuv422P/yuv444P等格式
  virtual void render(const YUVFrame& frame) = 0;
};

struct FlipParamet {
  // 是否X倒转
  int32_t bFlipX = false;
  int32_t bFlipY = false;
  inline bool operator==(const FlipParamet& right) const {
    return bFlipX == right.bFlipX && bFlipY == right.bFlipY;
  }
};

// ARGB<->BGRA<->RGBA<->RRRR
struct MapChannelParamet {
  int32_t red = 0;
  int32_t green = 1;
  int32_t blue = 2;
  int32_t alpha = 3;
  inline bool operator==(const MapChannelParamet& right) const {
    return red == right.red && green == right.green && blue == right.blue &&
           alpha == right.alpha;
  }
};

// 纹理混合
struct BlendParamet {
  float centerX = 0.0f;
  float centerY = 0.0f;
  float width = 0.4f;
  float height = 0.4f;
  // 显示如上位置图像的透明度
  float alaph = 0.2f;

  inline bool operator==(const BlendParamet& right) const {
    return centerX == right.centerX && centerY == right.centerY &&
           width == right.width && height == right.height &&
           alaph == right.alaph;
  }
};

// width/height 变成height/width
struct TransposeParamet {
  // 是否X倒转
  int32_t bFlipX = false;
  int32_t bFlipY = false;
  inline bool operator==(const TransposeParamet& right) const {
    return bFlipX == right.bFlipX && bFlipY == right.bFlipY;
  }
};

struct ReSizeParamet {
  int32_t bLinear = 1;
  int32_t newWidth = 0;
  int32_t newHeight = 0;
  inline bool operator==(const ReSizeParamet& right) const {
    return newWidth == right.newWidth && newHeight == right.newHeight &&
           bLinear == right.bLinear;
  }
};

struct SizeScaleParamet {
  int32_t bLinear = 1;
  float fx = 1.f;
  float fy = 1.f;
  inline bool operator==(const SizeScaleParamet& right) const {
    return fx == right.fx && fy == right.fy && bLinear == right.bLinear;
  }
};

// 跨 VkDevice 共享句柄
struct VkSharedHandle {
  uint64_t memHandle = 0;  // 内存 NT 句柄(VkImportMemoryWin32HandleInfoKHR)
  // Android: AHardwareBuffer*(VkImportAndroidHardwareBufferInfoANDROID)。
  // 所有权语义与 memHandle 相同: getVkOutputHandle 后转移给调用方释放
  void* ahb = nullptr;
  // Apple: IOSurfaceRef(VkImportMetalIOSurfaceInfoEXT)。非所有权: avox 持有,
  // 调用方不得 CFRelease, 跨帧留存必须 CFRetain; 图重建/尺寸变化会重建面,
  // 是否换面以 ioSurfaceId 变化为准(勿用指针比较)。像素格式按
  // IOSurfaceGetPixelFormat 查询(Vulkan 输出为 'BGRA'), 勿硬编码
  void* ioSurface = nullptr;
  uint64_t ioSurfaceId = 0;  // IOSurfaceGetID(), 换面探测
};

extern "C" {

AVOX_EXPORT void addInputeOb(IVInputLayer* layer, IVInputLayerOb* ob);
AVOX_EXPORT void removeInputeOb(IVInputLayer* layer, IVInputLayerOb* ob);

AVOX_EXPORT void addOutputOb(IVOutputLayer* layer, IVOutputLayerOb* ob);
AVOX_EXPORT void removeOutputOb(IVOutputLayer* layer, IVOutputLayerOb* ob);

AVOX_EXPORT void addSurfaceRenderOb(ISurfaceRender* surfaceRender,
                                   ISurfaceRenderOb* ob);
AVOX_EXPORT void removeSurfaceRenderOb(ISurfaceRender* surfaceRender,
                                      ISurfaceRenderOb* ob);

// https://cloud.tencent.com/developer/ask/sof/115847757
// Electorn当在32位和64位应用程序之间共享句柄时
// 只有较低的32位是重要的，因此截断句柄是安全的(当它从64位传递到32位时
AVOX_EXPORT void setElectronSurface(ISurfaceRender* surfaceRender,
                                   uint32_t handle);
// window返回WindowRender输出的DX11共享NT句柄
AVOX_EXPORT void* getRenderSharedHandle(ISurfaceRender* surfaceRender);

// ── VkDevice-VkDevice 输出: AVOX 写,外部读 ──
// 创建可导出的 VkSharedImage,每帧自动从管线拷入
// Apple: 数据通路常开,仅校验 IOSurface 就绪; w/h 不干预,出图分辨率跟随管线
AVOX_EXPORT bool enableVkOutput(ISurfaceRender* sr, int32_t w, int32_t h);
// 获取导出的 NT 句柄,外部在另一个 VkDevice 上导入
// Apple: 填 ioSurface/ioSurfaceId(非所有权,见 VkSharedHandle 注释);
// 未就绪返回 false,消费端轮询
AVOX_EXPORT bool getVkOutputHandle(ISurfaceRender* sr, VkSharedHandle* out);
// 断开输出交互
AVOX_EXPORT void disableVkOutput(ISurfaceRender* sr);

// ── D3D11 输出: AVOX 自建 NT 共享纹理,VK 每帧拷入,外部 DX11 设备打开复制 ──
// 在 avox 自身 D3D11 设备上创建 MISC_SHARED_NTHANDLE 纹理并导入 VK 管线
// (需 AVOX_ENABLE_VULKAN; 图未建时自动在构建后生效)
AVOX_EXPORT bool enableVkOutputDx11(ISurfaceRender* sr);
// 共享纹理 NT 句柄: 外部 DX11 设备 OpenSharedResource1 使用
// 图未建/绑定完成前返回 0,需轮询; 分辨率变化(重绑)后需重新获取
AVOX_EXPORT uint64_t getVkOutputDx11Handle(ISurfaceRender* sr);
// 共享 fence NT 句柄: 外部 OpenSharedFence 后 GetCompletedValue 轮询同步
// 每帧拷入完成后 avox 会 Signal 一次 (0 表示不可用)
AVOX_EXPORT uint64_t getVkOutputDx11FenceHandle(ISurfaceRender* sr);
// 关闭 D3D11 共享输出并重置管线
AVOX_EXPORT void disableVkOutputDx11(ISurfaceRender* sr);

// ── VkDevice-VkDevice 输入: 外部写,AVOX 读 ──
// 准备输入层,每帧自动从 sharedImage 拷到管线
AVOX_EXPORT bool enableVkInput(ISurfaceRender* sr, int32_t w, int32_t h);
// 传入外部导出的 NT 句柄,AVOX 在自己的 VkDevice 上导入
AVOX_EXPORT bool setVkInputHandle(ISurfaceRender* sr,
                                 const VkSharedHandle* handle);
// 断开输入交互
AVOX_EXPORT void disableVkInput(ISurfaceRender* sr);

AVOX_EXPORT const char* getVRenderTypeStr(RenderType type);

// 静态图像渲染器工厂
AVOX_EXPORT IImageRender* createImageRender();
// 按帧宽高比猜一份VR参数(启发式: ~2:1判OU全景, ~4:1判SBS全景,
// 16:9判SBS鱼眼180), 覆盖大多数片源; 判错的由宿主手改后 enableVr
AVOX_EXPORT bool guessVrParamet(int32_t width, int32_t height,
                                VrParamet* out);
}

}