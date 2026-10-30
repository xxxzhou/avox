#pragma once

#include "AvoxBase.h"
#include "AvoxLayer.h"
#include "AvoxMuxer.h"
#include "AvoxSource.h"
#include "AvoxVideo.h"

namespace avox {

// ============== 多输入采集会话 ==============
// 把 N 路设备输入变成 N 个文件 + 一份会话元数据; 会话本身不做合成/混音
// (收尾的混音与封装见文件末尾 IMediaComposer)。
// 与 ISourcePlayer 并列: 设备推流用 SourcePlayer, 录屏用本接口。
// 源指针归设备管理器所有, add* 之后、stop 之前禁止 refreshDevices。
// 失败语义: 会话失败只落 failed 态并回调 onCaptureError(源回调线程里不能同步
// 收尾), 宿主收到后应调 stop() 做清理(零帧视频文件会在 stop 时删除)。
// ⚠️ 不要在 onCaptureState/onCaptureError 回调线程内同步调 stop()/pause()/
// resume(): stop 会 join 报错的采集线程, 自己 join 自己会挂死 —— 回调里只置
// 标记, 回到宿主线程再调

// 会话状态
#define AVOX_MAP_CAPTURE_STATE(XX)   \
  XX(idle, 0, "idle")                \
  XX(starting, 1, "starting")        \
  XX(recording, 2, "recording")      \
  XX(paused, 3, "paused")            \
  XX(stopping, 4, "stopping")        \
  XX(stopped, 5, "stopped")          \
  XX(failed, 6, "failed")

enum class CaptureState : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_CAPTURE_STATE(XX)
#undef XX
};

AVOX_EXPORT const char* getCaptureStateStr(CaptureState state);

struct CaptureVideoInput {
  IVideoSource* source = nullptr;  // 设备管理器所有; 同一源禁止被两个会话同时用
  const char* id = "screen";       // 落盘名与工程标识
  // 腿级采集参数: 数值 0 字段回退会话 cfg.video(光标/边框恒会话级)。
  // 显示器腿必须给矩形(虚拟屏绝对物理像素); 窗口腿可不给(尺寸首帧回报)
  CaptureVideoParam param = {};
};

struct CaptureAudioInput {
  IAudioSource* source = nullptr;
  const char* id = "mic";
};

// start 前设置; start 后修改被忽略并告警(容器/编码器在打开时锁死)
struct CaptureConfig {
  // 下发主视频源; fps/cursor/border 会话会显式覆盖(录屏口径=60fps/关光标)
  CaptureVideoParam video = {};
  // 输出视频规格: 宽高<=0 时由采集矩形推导; fps<=0 用 video.fps
  VideoDesc outVideo = {};
  VideoEncodeParam videoEncode = {};
  VCodecId videoCodec = VCodecId::h265;
  // 中间容器扩展名(不带点); 本期音轨不进容器(分轨 WAV, finalize 合并)
  const char* container = "mkv";
};

struct CaptureStats {
  double achievedFps = 0;      // 近 2s 滑动窗口
  int64_t pushedFrames = 0;
  int64_t missedTicks = 0;     // 采集节拍跳过的槽位数(慢帧)
  int64_t droppedBySize = 0;   // 尺寸守卫丢弃(帧尺寸≠声明输出)
  int64_t droppedByFpsGate = 0;  // 帧率门丢弃(超频帧)
  int64_t encoderDropped = 0;  // 编码器拒收(send EAGAIN, 高码率/低性能机暴露面)
  int64_t elapsedMs = 0;
  int64_t bytesWritten = 0;
};

// 采集矩形: 虚拟屏绝对物理像素 + 内容尺寸(偶数对齐后)
struct CaptureGeometry {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
};

class ICaptureSessionOb {
 public:
  virtual ~ICaptureSessionOb() = default;

 public:
  virtual void onCaptureState(CaptureState pre, CaptureState cur) {}
  virtual void onCaptureError(const char* id, AVError err, const char* msg) {}
  virtual void onCaptureStats(const CaptureStats& stats) {}
};

class ICaptureSession {
 public:
  virtual ~ICaptureSession() = default;

 public:
  // 第一路视频即 primary: 决定几何/时钟/失败语义
  virtual bool addVideoInput(const CaptureVideoInput& in) = 0;
  virtual bool addAudioInput(const CaptureAudioInput& in) = 0;
  virtual bool setConfig(const CaptureConfig& cfg) = 0;
  virtual void setObserver(ICaptureSessionOb* ob) = 0;
  // 配置根(rec.* 等长尾键; 固定能力走 setConfig)
  virtual IOption* getOption() = 0;
  virtual bool start(const char* dir) = 0;
  virtual void stop() = 0;
  // 暂停段从时间线剔除: 视频 pts 扣暂停累计(文件 pts 连续), 音频不落盘
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual CaptureState getState() = 0;
  virtual CaptureStats getStats() = 0;
  virtual bool getCaptureGeometry(CaptureGeometry* geo) = 0;
  // 会话时钟当前毫秒(已扣暂停), start 前返回 -1; 光标采样用它对时
  virtual int64_t getClockMs() = 0;
  // primary 转换腿的渲染面; 宿主 setSurface 挂预览, 摘掉不影响录制
  virtual ISurfaceRender* getPreviewRender() = 0;
};

AVOX_EXPORT ICaptureSession* createCaptureSession();

// ============== 编辑场景(录屏包装导出: 背景/圆角/阴影/比例 + 镜头/光标/涟漪) ==============
// 单趟计算层(VkSceneLayer, 2 入 1 出)挂在 Vulkan 渲染图 yuv2RGBA 之后;
// 预览与导出走同一渲染, 求值只有这一份。仅 Vulkan 车道, forceHDR 拒绝(直通
// 拓扑裁掉全部可选层, 场景会静默消失)。

// 静态样式(R1-R4); 占比字段均相对画布宽
struct SceneStyle {
  // 背景纵向渐变两色(0-1, gamma 域); 纯色时两者相同
  float bgColorTop[3] = {0.055f, 0.065f, 0.085f};
  float bgColorBottom[3] = {0.055f, 0.065f, 0.085f};
  // 内容区与画布边的留白(内容区按源比例适配后居中)
  float padding = 0.0f;
  // 内容区圆角半径
  float cornerRadius = 0.0f;
  // 阴影(SDF 近似, 边缘比真高斯实; v1 接受)
  float shadowRadius = 0.0f;
  float shadowAlpha = 0.0f;
  float shadowOffsetX = 0.0f;
  float shadowOffsetY = 0.0f;
  // 光标精灵大小(画布高占比; 图集格子按此缩放)
  float cursorScale = 0.03f;
};

// 镜头段: pts∈[t0,t1) 用源像素矩形(x,y,w,h)裁切映射到内容区;
// t1<=0 = 直到最后。easeIn/easeOut 为缓动毫秒(预留, P1 直切);
// 超出段表 = 前后段延伸(首段向前、末段向后)
struct SceneCameraSegment {
  int64_t t0 = 0;
  int64_t t1 = 0;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  int32_t easeIn = 0;
  int32_t easeOut = 0;
  int32_t follow = 0;
};

// 光标采样(宿主/shim 遥测), t=会话时钟毫秒; shape=图集格子序号
struct SceneCursorSample {
  int64_t t = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t shape = 0;
  int32_t visible = 0;
};

struct SceneClick {
  int64_t t = 0;
  int32_t x = 0;
  int32_t y = 0;
  // 涟漪强度 0-1(0 = 只画光标变化不画涟漪)
  float strength = 0.6f;
};

// 控制侧句柄: set* 即拷贝快照(宿主栈上数组也安全), 渲染线程按代数取新
class IEditorScene {
 public:
  virtual ~IEditorScene() = default;

 public:
  virtual void setStyle(const SceneStyle& style) = 0;
  // 画布尺寸=导出目标分辨率; 场景挂载时 setVideoDesc 尺寸必须与此相等
  virtual void setCanvas(int32_t width, int32_t height) = 0;
  virtual void setSegments(const SceneCameraSegment* segs, int32_t count) = 0;
  virtual void setCursorTrack(const SceneCursorSample* samples,
                              int32_t count) = 0;
  // 图集: CPU RGBA, cols*rows 格子; 尺寸变化要重建图(detach 后重挂)
  virtual void setCursorAtlas(IImageBuffer* atlas, int32_t cols,
                              int32_t rows) = 0;
  virtual void setClicks(const SceneClick* clicks, int32_t count) = 0;
};

AVOX_EXPORT IEditorScene* createEditorScene();
AVOX_EXPORT void destroyEditorScene(IEditorScene* scene);
// 挂到渲染面: open 前或首帧前; 之后挂触发图重建。
// ⚠️ detach 之前场景对象必须存活(渲染图持有其指针, detach/重挂后才能 destroy)
AVOX_EXPORT bool attachEditorScene(ISurfaceRender* render, IEditorScene* scene);
AVOX_EXPORT void detachEditorScene(ISurfaceRender* render);

// ============== 录制收尾: 混音与封装 ==============
// 采集阶段只落 WAV + 视频流; 收尾/工程导出时把多路 WAV 混成一条 AAC, 再与视频
// 流直拷封装。输入 WAV 自解析(引擎 FFmpeg 白名单不保证带 pcm_f32le 解码器)。

// 混音结果(值导出, 只增不改)
enum class MediaMixStatus : int32_t {
  ok = 0,
  // 没有可混的轨(空/静音/区间为空), 不算失败
  noAudio = 1,
  failed = 2,
};

class IMediaComposer {
 public:
  virtual ~IMediaComposer() = default;

 public:
  // 清空轨道列表与错误态, 复用对象再跑一次
  virtual void reset() = 0;
  // startMs = 该轨首包的会话时钟(与视频 pts0 同一时间线); <0 或空路径 = 本轨缺席
  virtual void addAudioTrack(const char* wavPath, int64_t startMs, float gainDb,
                             bool mute) = 0;
  // 多轨混音 → 48k 立体声 AAC(容器按 outPath 后缀推断, 通常 .m4a)
  // [startMs, endMs) 是源时间; videoPts0Ms 是视频首帧的会话时钟
  virtual MediaMixStatus mixAudio(int64_t videoPts0Ms, int64_t startMs,
                                  int64_t endMs, const char* outPath) = 0;
  // 视频/音频流直拷进目标容器(audioPath 可为 nullptr/空): mp4 加 faststart,
  // HEVC 在 mp4/mov 打 hvc1
  virtual bool remux(const char* videoPath, const char* audioPath,
                     const char* outPath) = 0;
  // 最近一次失败原因(成功为空串); 指针在下一次操作/reset 前有效
  virtual const char* getLastError() = 0;
};

extern "C" {
AVOX_EXPORT IMediaComposer* createMediaComposer();
}

}