#pragma once

#include "AvoxSdk.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <stdint.h>

// 轮询事件 (avox线程入队, Unity主线程poll), 对应 AvoxNative.NativeEvent 布局
struct AvoxUnityEvent {
  enum class EType : int32_t { none = 0, state = 1, ready = 2, complete = 3, error = 4 };
  int32_t type = 0;   // EType
  int32_t state = 0;  // avox::PlayerState (type==state)
  int32_t code = 0;   // AVError/DecodeResult 数值 (type==error)
  int32_t reserved = 0;
  char msg[240] = {0};
};

// 注册表查找: id → bridge (纹理更新回调/AvoxUnityApi 共用)
class PlayerBridge;
PlayerBridge* findBridge(uint32_t id);

// avox 播放器包装 + 纹理帧桥接 (对应 godot 插件 SurfaceTextureBridge + PlayerOb 合体)
//
// GPU 直通模式 (Unity Vulkan 后端, volk 就绪):
//   onReady 尺寸就绪 → 主线程 updateGpu(): enableVkOutput + getVkOutputHandle
//   → NT 句柄导入 Unity VkDevice → importedImage 交给 C# CreateExternalTexture 收养
// CPU 回退模式 (其他图形后端 / 初始化失败):
//   avox 渲染线程 onFrame 转 BGRA 进单帧槽位, Unity 经
//   IssuePluginCustomTextureUpdateV2 回调取帧, GPU 上传由 Unity 完成
class PlayerBridge : public avox::IMediaPlayerOb, public avox::ISurfaceRenderOb {
 public:
  explicit PlayerBridge(uint32_t id);
  ~PlayerBridge() override;

  uint32_t id() const { return id_; }

  // ── 配置 (open 前调用) ──
  void setHardDecode(bool bEnable);
  void setVolume(float volume);
  void setIoPlan(int32_t plan);
  void setSpeed(double speed);

  // ── 控制 ──
  void open(const char* url);
  void close();
  void pause();
  void resume();
  void seek(int64_t pos);
  // 状态/信息 (getDuration 等主线程直调, 同 godot 插件用法)
  int32_t state() const { return stateCache_.load(); }
  int64_t duration() const;
  int64_t position() const;
  double progress() const;
  // 事件弹出一条, 无事件返回 false
  bool pollEvent(AvoxUnityEvent* out);
  // 帧尺寸 (GPU=导入纹理尺寸, CPU=BGRA槽尺寸), 无帧返回 false
  bool frameInfo(int32_t* w, int32_t* h);
  // 是否 GPU 直通模式
  bool gpuMode() const { return gpuMode_; }
  // GPU 导入的 VkImage (Unity CreateExternalTexture 用), 未就绪返回 0
  uint64_t gpuImage() const { return importedImage_; }

  // ── GPU 直通主线程处理 (Unity Update 里调):
  // volk 延迟初始化 + enableVkOutput + NT 句柄导入 Unity VkDevice + 尺寸变化重导
  void updateGpu();

  // CPU 路径回调取帧 (IssuePluginCustomTextureUpdateV2 UpdateTextureBegin):
  // 按 Unity 纹理尺寸分配 BGRA 数据, 无帧/尺寸不符时补黑边, 失败返回 false
  bool allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData);

 private:
  // ── avox::IMediaPlayerOb (avox 线程) ──
  void onStateChange(avox::PlayerState preState, avox::PlayerState state) override;
  void onReady() override;
  void onComplete() override;
  void onIoError(avox::AVError error, const char* msg) override;
  void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override;

  // ── avox::ISurfaceRenderOb (avox 渲染线程) ──
  void onFrame(const avox::YUVFrame& frame) override;
  void onWinSizeChange(int32_t width, int32_t height) override;

  void createPlayer();
  void destroyPlayer();
  void bindSurface();
  void unbindSurface();
  void startGpuImport(int32_t w, int32_t h);
  void releaseGpuImport();
  void pushState(int32_t state);
  void pushError(int32_t code, const char* msg);
  void pushEvent(const AvoxUnityEvent& e);

  static void convertNv12(const avox::YUVFrame& frame, uint8_t* dst);
  static void convertYuv420P(const avox::YUVFrame& frame, uint8_t* dst);

  uint32_t id_;
  avox::IMediaPlayer* player_ = nullptr;
  avox::ISurfaceRender* surface_ = nullptr;

  // ── 配置缓存 (open 前设置) ──
  bool hardDecode_ = true;
  float volume_ = 1.0f;
  int32_t ioPlan_ = 0;
  double speed_ = 1.0;

  // ── GPU 直通状态 ──
  bool gpuMode_ = false;        // 绑定时按全局可用性决定
  bool gpuOutputOn_ = false;    // enableVkOutput 已调用
  std::atomic<bool> pendingGpuInit_{false};    // 尺寸就绪待主线程导入
  std::atomic<bool> pendingGpuResize_{false};  // 尺寸变化待主线程重导
  std::atomic<int32_t> videoW_{0};
  std::atomic<int32_t> videoH_{0};
  uint64_t importedImage_ = 0;  // VkImage (Unity CreateExternalTexture 收养)
  uint64_t importedMemory_ = 0;
  int32_t gpuW_ = 0;
  int32_t gpuH_ = 0;

  // ── CPU 帧槽 (mutex, 新帧覆盖旧帧) ──
  std::mutex frameMutex_;
  std::vector<uint8_t> frameBgra_;
  int32_t frameW_ = 0;
  int32_t frameH_ = 0;

  // ── 状态与事件队列 ──
  std::atomic<int32_t> stateCache_{0};
  std::mutex eventMutex_;
  std::deque<AvoxUnityEvent> events_;
};
