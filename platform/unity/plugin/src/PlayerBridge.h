#pragma once

#include "AvoxSdk.h"
#include "GpuPassthrough.h"

#include <atomic>
#include <chrono>
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

  uint32_t id() const { return playerId; }

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
  int32_t state() const { return stateCache.load(); }
  int64_t duration() const;
  int64_t position() const;
  double progress() const;
  // 事件弹出一条, 无事件返回 false
  bool pollEvent(AvoxUnityEvent* out);
  // 帧尺寸 (GPU=导入纹理尺寸, CPU=BGRA槽尺寸), 无帧返回 false
  bool frameInfo(int32_t* w, int32_t* h);
  // 是否 GPU 直通模式
  bool gpuMode() const { return bGpuMode; }
  // GPU 导入的 VkImage (Unity CreateExternalTexture 用), 未就绪返回 0
  uint64_t gpuImage() const { return importedImage; }

  // ── GPU 直通主线程处理 (Unity Update 里调):
  // volk 延迟初始化 + enableVkOutput/Dx11 + 句柄获取/导入 + 尺寸变化重导
  void updateGpu();

  // D3D11/D3D12 拷贝模式 (flavor 2/3): Unity 渲染线程事件入口
  // (GetRenderEventFunc → GL.IssuePluginEvent, eventId = id())
  // 打开共享纹理 + fence 去重 + CopyResource 到 C# 纹理 (按 flavor 分派 D3D11/D3D12)
  void renderDx11Copy();
  // C# 纹理 GetNativeTexturePtr (主线程设置, 渲染线程读取)
  void setDx11Target(void* nativeTex) { dx11Target.store(nativeTex); }
  // D3D12 模式 C# 纹理 nativeTex (拷贝目的)
  void setDx12Target(void* nativeTex) { dx12Target.store(nativeTex); }
  // 外部纹理句柄 (C# CreateExternalTexture 收养): D3D11=目标纹理 / D3D12=目标 SRV 描述符
  uint64_t dx11NativeTex();
  // 拷贝链路诊断: 事件数/实际拷贝数/目标缺失数/最近 fence 值/打开次数
  void dx11Debug(uint32_t* events, uint32_t* copies, uint32_t* targetNull,
                 uint64_t* fenceVal, uint32_t* opens) {
    if (events) *events = dbgEvents.load();
    const bool dx12Mode = (unityGpuImportFlavor() == 3);
    if (copies) *copies = dx12Mode ? dx12.copyCount : dx11.copyCount;
    if (targetNull) *targetNull = dbgTargetNull.load();
#ifdef _WIN32
    if (fenceVal) *fenceVal = dx12Mode ? unityDx12FenceValue(&dx12) : dbgFenceVal.load();
#endif
    if (opens) *opens = dx12Mode ? dx12.openCount : dx11.openCount;
  }

  // D3D12 拷贝链路诊断: 目标未解析次数/无命令列表次数/拷贝数/打开次数
  void dx12Debug(uint32_t* noTarget, uint32_t* noCl, uint32_t* copies, uint32_t* opens) {
    if (noTarget) *noTarget = dx12.noTarget;
    if (noCl) *noCl = dx12.noCl;
    if (copies) *copies = dx12.copyCount;
    if (opens) *opens = dx12.openCount;
  }
  // 诊断: 当前共享纹理 NT 句柄快照 (转储用)
  uint64_t dx11HandleSnapshot() const { return dx11Handle.load(); }

  // CPU 路径回调取帧 (IssuePluginCustomTextureUpdateV2 UpdateTextureBegin):
  // Unity 纹理是 R8 的 w × h*3/2 (整帧 NV12), 无帧/尺寸不符时补黑边, 失败返回 false
  bool allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData);
  // 源色彩空间编码 (standard | range<<8), 供 C# 构 shader 矩阵; 未就绪返回 -1
  int32_t colorSpaceCode() const;
  // packed 帧 → 紧凑 NV12 (SourceBridge/RtcPlayerBridge CPU 路径复用)。
  // buf 恒为 packed 布局 (avox 契约): Y 行距 rowPitch, nv12 的 UV 交织行距 rowPitch,
  // yuv420P 的 UV 打包为 [U: h/4 物理行 [偶|奇|pad]] + [V: 同构]。
  // dst 需 w*h*3/2 字节; yuv420P 会交织成 NV12, 使下游 shader 只认一种布局。
  // 返回 false 表示格式/尺寸不支持 (未写 dst)
  static bool PackNv12(avox::IImageBuffer* buf, avox::YuvType type, uint8_t* dst);

  // ── Option (键值参数, 透传 IMediaPlayer::getOption) ──
  bool setOptionBool(const char* key, bool value);
  bool setOptionInt(const char* key, int64_t value);
  bool setOptionNumber(const char* key, double value);
  bool setOptionString(const char* key, const char* value);
  // avox::ArgType 数值 (0 null 1 bool 2 int 3 number 4 string), 无 player 返回 -1
  int32_t optionType(const char* key);
  int64_t optionInt(const char* key);
  double optionNumber(const char* key);
  // 写入调用方缓冲(含\0, 缓冲不足截断), 返回长度; 键不存在/类型不符返回 -1
  int32_t optionString(const char* key, char* buf, int32_t bufSize);
  // ── 录制 (IMediaPlayer::getMuxer, ffmpeg 封装, 录制中再调会先停旧) ──
  bool startRecord(const char* path, bool bTranscode);
  void stopRecord();
  // avox::RecorderState 数值 (0 none 1 opening 2 recording 3 completed)
  int32_t recordState();
  // ── 字幕 (SRT 文件; ASR/翻译依赖可选模块, 未集成时 getSubtitle 为空) ──
  bool loadSrt(const char* path);
  void closeSubtitle();

 private:
  // ── avox::IMediaPlayerOb (avox 线程) ──
  void onStateChange(avox::PlayerState preState, avox::PlayerState state) override;
  void onReady() override;
  void onComplete() override;
  void onIoError(avox::AVError error, const char* msg) override;
  void onDecodeError(avox::TrackType trackType, avox::DecodeResult error) override;

  // ── avox::ISurfaceRenderOb (avox 渲染线程) ──
  void onFrame(avox::IImageBuffer* buf, avox::YuvType yuvType) override;
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
  avox::IOption* option() const;


  uint32_t playerId;
  avox::IMediaPlayer* player = nullptr;
  avox::ISurfaceRender* surface = nullptr;
  avox::ColorSpaceDesc colorSpace;  // onReady 时取自源 VideoDesc, shader/渲染共用
  bool colorSpaceSet = false;       // 未就绪时 colorSpaceCode() 返回 -1
  avox::IMediaMuxer* muxer = nullptr;

  // ── 配置缓存 (open 前设置) ──
  bool hardDecode = true;
  float volume = 1.0f;
  int32_t ioPlan = 0;
  double speed = 1.0;

  // ── GPU 直通状态 ──
  bool bGpuMode = false;        // 绑定时按全局可用性决定
  bool gpuOutputOn = false;    // enableVkOutput/enableVkOutputDx11 已调用
  bool gpuErrorPushed = false; // 失败错误只推一次 (重试期不刷事件)
  uint32_t gpuRetry = 0;       // 重试帧计数 (超时判断配合)
  std::chrono::steady_clock::time_point gpuRetryStart{};  // 重试窗口起点 (时间制)
  std::atomic<bool> pendingGpuInit{false};    // 尺寸就绪待主线程导入
  std::atomic<bool> pendingGpuResize{false};  // 尺寸变化待主线程重导
  std::atomic<int32_t> videoW{0};
  std::atomic<int32_t> videoH{0};
  uint64_t importedImage = 0;  // VkImage (Unity CreateExternalTexture 收养)
  uint64_t importedMemory = 0;
  void* importedAhb = nullptr;  // Android: avox 转移来的 AHardwareBuffer (release 随 releaseGpuImport)
  int32_t gpuW = 0;
  int32_t gpuH = 0;

  // ── D3D11/D3D12 拷贝模式 (flavor 2/3, 产出同为 enableVkOutputDx11) ──
  // dx11/dx12 状态仅渲染线程触碰; 句柄/尺寸跨线程用 atomic
  std::atomic<uint64_t> dx11Handle{0};      // avox 共享纹理 NT 句柄 (avox 持有, 勿 CloseHandle)
  std::atomic<uint64_t> dxFenceHandle{0};   // 共享 fence NT 句柄 (可空)
  std::atomic<int32_t> dx11W{0};            // 打开后由渲染线程回填实际尺寸
  std::atomic<int32_t> dx11H{0};
  std::atomic<void*> dx11Target{nullptr};   // C# 纹理 nativeTex
  std::atomic<void*> dx12Target{nullptr};   // D3D12 模式 C# 纹理 nativeTex (拷贝目的)
  std::atomic<bool> dx11PendingClose{false};// 释放请求转交渲染线程执行
  std::atomic<uint32_t> dbgEvents{0};       // 诊断: 渲染事件次数
  std::atomic<uint32_t> dbgTargetNull{0};   // 诊断: 目标为空的次数
  std::atomic<uint64_t> dbgFenceVal{0};     // 诊断: 最近一次观察到的 fence 值
  UnityDx11CopyState dx11;
  UnityDx12CopyState dx12;

  // ── CPU 帧槽 (mutex, 新帧覆盖旧帧); 紧凑 NV12, 尺寸 frameW × frameH*3/2 ──
  std::mutex frameMutex;
  std::vector<uint8_t> frameNv12;
  int32_t frameW = 0;
  int32_t frameH = 0;

  // ── 状态与事件队列 ──
  std::atomic<int32_t> stateCache{0};
  std::mutex eventMutex;
  std::deque<AvoxUnityEvent> events;
};
