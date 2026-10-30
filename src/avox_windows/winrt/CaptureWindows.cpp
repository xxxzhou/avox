#include "CaptureWindows.hpp"

#include "avox/module/AvoxManager.hpp"

extern "C" {

HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
    ::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);

HRESULT __stdcall CreateDirect3D11SurfaceFromDXGISurface(
    ::IDXGISurface* dgxiSurface, ::IInspectable** graphicsSurface);
}
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
IDirect3DDxgiInterfaceAccess : ::IUnknown {
  virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

namespace avox {

void regWinCaptureDevice() {
  RegFunc regFunc = {"win capture device init", []() {
                       AvoxManager::Get().vDeviceMgr.regMgrObj(
                           VDeviceSdk::win_capture, []() -> IVideoManager* {
                             return new CaptureWindowsMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

// 运行时探测 Windows Graphics Capture 是否可用(Win10 1903+,
// UniversalApiContract 7)。
bool isWinRtCaptureAvailable() {
  static bool available = []() {
    try {
      return winrt::Windows::Foundation::Metadata::ApiInformation::
          IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 7);
    } catch (...) {
      return false;
    }
  }();
  return available;
}

void WinCaptureBase::setWindow(HWND hwnd_) {
  hwnd = hwnd_;
  deviceName = getWindowName(hwnd);
  deviceId = getWindowName(hwnd);
  deviceKind = VDeviceKind::window;
  // 类名 + 进程名: -l 诊断用 (区分 UWP ApplicationFrameWindow 框架窗 vs CoreWindow 内容窗)
  char cls[256] = {};
  if (GetClassNameA(hwnd, cls, sizeof(cls))) windowClass = cls;
  processName = getWindowProcessName(hwnd);
}

void WinCaptureBase::setMonitor(HMONITOR hmon_, int32_t index) {
  hmon = hmon_;
  hwnd = nullptr;
  MONITORINFOEXW mi = {};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(hmon, &mi)) {
    char devId[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, devId, sizeof(devId),
                        nullptr, nullptr);
    deviceId = devId;
  }
  deviceName = "monitor " + std::to_string(index);
  deviceKind = VDeviceKind::monitor;
}

bool CaptureWindows::onOpen() {
  if (!hwnd && !hmon) {
    return false;
  }
  // 窗口采集: 打开时若窗口最小化/隐藏, 恢复一次可见(仅本周期首次; 桌面采集hwnd空会跳过)
  restoreWindowForCapture(hwnd);
  startTask();
  return true;
}

void CaptureWindows::onClose() { stopTask(); }

bool CaptureWindows::bOpening() { return running(); }

void CaptureWindows::onRunTask() {
  if (!device) {
    createDevice11(&device, &context);
    setDevice(device.Get());
  }
  const bool bStrict = bCaptureParamSet;
  // GDI BitBlt 本身不含光标
  cursorMode = "absent";
  // 采集矩形解析(一次): 显示器目标 region∩显示器, 窗口目标 region∩窗口矩形。
  // BitBlt 源坐标: 屏幕 DC=虚拟屏绝对坐标, 窗口 DC=窗口相对坐标
  int32_t srcX = 0;
  int32_t srcY = 0;
  int32_t outW = 0;
  int32_t outH = 0;
  int32_t absX = 0;
  int32_t absY = 0;
  bool bRegion = false;
  if (bStrict && captureParam.regionW > 1 && captureParam.regionH > 1) {
    int32_t rx = captureParam.regionX;
    int32_t ry = captureParam.regionY;
    int32_t rr = captureParam.regionX + captureParam.regionW;
    int32_t rb = captureParam.regionY + captureParam.regionH;
    if (hmon) {
      MONITORINFOEXW mi = {};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(hmon, &mi)) {
        rx = std::max(rx, (int32_t)mi.rcMonitor.left);
        ry = std::max(ry, (int32_t)mi.rcMonitor.top);
        rr = std::min(rr, (int32_t)mi.rcMonitor.right);
        rb = std::min(rb, (int32_t)mi.rcMonitor.bottom);
        if (rr - rx >= 2 && rb - ry >= 2) {
          // 屏幕 DC 的源坐标即虚拟屏绝对坐标
          srcX = rx;
          srcY = ry;
          absX = rx;
          absY = ry;
          outW = (rr - rx) & ~1;
          outH = (rb - ry) & ~1;
          bRegion = true;
        }
      }
    } else if (hwnd) {
      RECT wr = {};
      if (GetWindowRect(hwnd, &wr)) {
        rx = std::max(rx, (int32_t)wr.left);
        ry = std::max(ry, (int32_t)wr.top);
        rr = std::min(rr, (int32_t)wr.right);
        rb = std::min(rb, (int32_t)wr.bottom);
        if (rr - rx >= 2 && rb - ry >= 2) {
          srcX = rx - (int32_t)wr.left;
          srcY = ry - (int32_t)wr.top;
          absX = rx;
          absY = ry;
          outW = (rr - rx) & ~1;
          outH = (rb - ry) & ~1;
          bRegion = true;
        }
      }
    }
  }
  CaptureInfo capInfo = {};
  capInfo.cursorMode = "absent";
  if (bRegion) {
    capInfo.regionX = absX;
    capInfo.regionY = absY;
    capInfo.regionW = outW;
    capInfo.regionH = outH;
  }
  dispatch(&IVideoSourceOb::onCaptureInfo, capInfo);
  double fps = (bStrict && captureParam.fps > 0) ? captureParam.fps : 30.0;
  int64_t fpsTick = getFrameTick(fps);
  // 节拍: 绝对到期(t0+tick*period), pts=排期时刻而非墙钟; tick 由时钟反推,
  // 慢帧跳拍记 missedTicks —— 与 WGC 严格路同构, 计数器硬推会把整段录成快放
  int64_t t0 = timeTick();
  int64_t tick = 0;
#ifdef WIN32
  // 15.6ms 系统定时器粒度会让高帧率节拍发散; 1ms 粒度下绝对到期才准
  timeBeginPeriod(1);
#endif
  while (running()) {
    int64_t due = t0 + tick * fpsTick;
    while (running() && timeTick() < due) {
      sleepToTick(due);
    }
    if (!running()) {
      break;
    }
    // sleepToTick 粗睡到差 1ms, 最后 1ms 自旋到点
    while (timeTick() < due) {
    }
    int64_t framePts = (int64_t)((double)tick * 1000.0 / fps + 0.5);
    int64_t prevTick = tick;
    tick = (timeTick() - t0) / fpsTick + 1;
    if (tick > prevTick + 1) {
      missedTicks += tick - prevTick - 1;
    }
    int32_t width = outW;
    int32_t height = outH;
    if (!bRegion) {
      // 旧路跟窗口/显示器尺寸走(尺寸变化重建纹理); 显示器源坐标=显示器原点
      srcX = 0;
      srcY = 0;
      if (hwnd) {
        if (!getWindowSize(hwnd, width, height)) {
          continue;
        }
      } else if (hmon) {
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(hmon, &mi)) {
          continue;
        }
        width = mi.rcMonitor.right - mi.rcMonitor.left;
        height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        srcX = mi.rcMonitor.left;
        srcY = mi.rcMonitor.top;
      } else {
        continue;
      }
    }
    if (width != frameFormat.width || height != frameFormat.height) {
      frameFormat.width = width;
      frameFormat.height = height;
      frameFormat.imageType = ImageType::bgra8;
      outSharedTex = std::make_unique<Dx11SharedTex>();
      Dx11Texture* outTexture = outSharedTex->getDx11Texture();
      // DXGI_FORMAT_B8G8R8A8_UNORM DXGI_FORMAT_R8G8B8A8_UNORM
      outTexture->setTextureSize(width, height, DXGI_FORMAT_B8G8R8A8_UNORM);
      outTexture->setGUI(true);
      outTexture->setNoView(true);
      outSharedTex->initTexture(device.Get());
      if (!outSharedTex->getDx11Texture()) {
        dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
                 "create texture error");
        continue;
      }
      setTexture(outSharedTex->getDx11Texture()->texture.Get());
    }
    if (!outSharedTex || !outSharedTex->getDx11Texture()) {
      continue;
    }
    ID3D11Texture2D* gdiTexture = outSharedTex->getDx11Texture()->texture.Get();
    // 得到窗口DC,如果中间返回自动释放
    HDC srcDc = hwnd ? ::GetWindowDC(hwnd) : ::GetDC(NULL);
    std::unique_ptr<HDC__, std::function<void(HDC)>> uhwnd(
        srcDc, [=](HDC x) { hwnd ? ReleaseDC(hwnd, x) : ReleaseDC(NULL, x); });
    MComPtr<IDXGISurface1> surface = nullptr;
    HRESULT hr =
        gdiTexture->QueryInterface(_uuidof(IDXGISurface1), (LPVOID*)&surface);
    if (FAILED(hr)) {
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "get texture surface error");
      continue;
    }
    HDC gdiHdc = nullptr;
    // 得到渲染纹理DC
    hr = surface->GetDC(FALSE, &gdiHdc);
    if (FAILED(hr)) {
      // logHResult(hr, "failed get hdc or get DXGI surface");
      surface->ReleaseDC(NULL);
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "get texture surface dc error");
      continue;
    }
    std::unique_ptr<HDC__, std::function<void(HDC)>> ugdiHdc(
        gdiHdc, [=](HDC) { surface->ReleaseDC(NULL); });
    auto targetCaps = GetDeviceCaps(uhwnd.get(), RASTERCAPS);
    auto gdiCaps = GetDeviceCaps(ugdiHdc.get(), RASTERCAPS);
    if (!(targetCaps & RC_BITBLT) || !(gdiCaps & RC_BITBLT)) {
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "get texture and window no support bitblt");
      continue;
    }
    auto err = BitBlt(ugdiHdc.get(), 0, 0, width, height, uhwnd.get(), srcX,
                      srcY, SRCCOPY);
    outSharedTex->signalFence();
    // 一定要有,否则数据抓不到
    ugdiHdc.reset();
    // 发送数据
    GpuFrame gpuFrame = {};
    gpuFrame.buffer = gdiTexture;
    gpuFrame.format.width = width;
    gpuFrame.format.height = height;
    gpuFrame.context = outSharedTex.get();
    gpuFrame.pts = bStrict ? framePts : timeStampMS();
    gpuFrame.dts = gpuFrame.pts;
    // checkSizeChanged(gpuFrame.format);
    onFrame(gpuFrame);
  }
#ifdef WIN32
  timeEndPeriod(1);
#endif
}

RTCaptureWindows::RTCaptureWindows() {}

RTCaptureWindows::~RTCaptureWindows() {
  // 本级先停线程(晚到 ~RunTask 时 framePool/session 已析构), 与 onClose 幂等
  stopTask();
}

bool RTCaptureWindows::onOpen() {
  if (!hwnd && !hmon) {
    return false;
  }
  // 窗口采集: 打开时若窗口最小化/隐藏, 恢复一次可见(仅本周期首次; 桌面采集hwnd空会跳过)
  restoreWindowForCapture(hwnd);
  // 实际抓帧在 onRunTask 线程里完成, 这里只启动任务线程。
  startTask();
  return true;
}

void RTCaptureWindows::onClose() {
  // stopTask 置 runflag=false 并 join; onRunTask 循环退出时已清理 framePool/session。
  stopTask();
}

bool RTCaptureWindows::bOpening() { return running(); }

void RTCaptureWindows::onRunTask() {
  // 本线程全程 MTA。framePool 的创建/TryGetNextFrame/Recreate 全在本线程,
  // 既无跨 apartment 问题(曾因 DispatcherQueue 的 STA 专用线程触发
  // RPC_E_WRONG_THREAD/0x8001010e -> CoreMessaging stowed 崩溃), 也无需
  // FrameArrived 回调(回调内同步 Recreate 会让帧池停止派发后续帧)。
  // 采集矩形解析结果(try 内赋值, 节拍循环在 try 外使用)
  int32_t srcBoxX = 0;
  int32_t srcBoxY = 0;
  int32_t absX = 0;
  int32_t absY = 0;
  int32_t alignedW = 0;
  int32_t alignedH = 0;
  bool bRegion = false;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (!device) {
      // 设备多线程, 兼容帧池内部 QI
      createDevice11(&device, &context, true);
      setDevice(device.Get());
    }
    MComPtr<IDXGIDevice> dxgiDevice = nullptr;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, "winrt capture get IDXGIDevice error");
      return;
    }
    winrt::com_ptr<IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                              inspectable.put());
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, "winrt capture failed to get WinRT device");
      return;
    }
    rtDevice = inspectable.as<winrt::IDirect3DDevice>();
    auto activationFactory =
        winrt::get_activation_factory<winrt::GraphicsCaptureItem>();
    auto interopFactory = activationFactory.as<IGraphicsCaptureItemInterop>();
    winrt::GraphicsCaptureItem item{nullptr};
    winrt::guid captureItemGuid =
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>();
    if (hwnd) {
      hr = interopFactory->CreateForWindow(
          hwnd, captureItemGuid,
          reinterpret_cast<void**>(winrt::put_abi(item)));
    } else {
      hr = interopFactory->CreateForMonitor(
          hmon, captureItemGuid,
          reinterpret_cast<void**>(winrt::put_abi(item)));
    }
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, hwnd ? "winrt capture failed to CreateForWindow"
                           : "winrt capture failed to CreateForMonitor");
      return;
    }
    // R8G8B8G8UIntNormalized B8G8R8A8UIntNormalized
    winrt::SizeInt32 poolSize = item.Size();
    framePool = winrt::Direct3D11CaptureFramePool::Create(
        rtDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
        poolSize);
    session = framePool.CreateCaptureSession(item);
    // 录屏会话(bCaptureParamSet): 关系统光标/黄框, 光标只存在于遥测数据里。
    // AVOX_CAPTURE_NO_TWEAK=1: A/B 诊断开关, 跳过两个 session 级调用
    // (Win11 部分构建上无权限的 IsBorderRequired(false) 会话会整段黑帧)
    if (bCaptureParamSet && std::getenv("AVOX_CAPTURE_NO_TWEAK") == nullptr) {
      cursorMode = "baked";
      try {
        auto session2 = session.try_as<
            winrt::Windows::Graphics::Capture::IGraphicsCaptureSession2>();
        if (session2) {
          session2.IsCursorCaptureEnabled(captureParam.captureCursor);
          if (!captureParam.captureCursor) {
            cursorMode = "disabled";
          }
        }
      } catch (...) {
        AVOX_WIN_LOG(E_FAIL, "winrt capture IsCursorCaptureEnabled failed");
      }
      try {
        auto session3 = session.try_as<
            winrt::Windows::Graphics::Capture::IGraphicsCaptureSession3>();
        if (session3 && !captureParam.border) {
          // 仅 Win11 生效; 无权限/老系统抛错, 保持默认黄框
          session3.IsBorderRequired(false);
        }
      } catch (...) {
        AVOX_WIN_LOG(E_FAIL, "winrt capture IsBorderRequired failed");
      }
    }
    // 不注册 FrameArrived, 改为下方按 fps 轮询 TryGetNextFrame
    session.StartCapture();
    // 采集矩形解析: 录屏会话 + 显示器目标才应用区域裁剪(虚拟屏绝对→显示器内坐标);
    // 窗口目标/未设参数保持全画面(与旧路径一致)
    alignedW = poolSize.Width + (poolSize.Width % 2);
    alignedH = poolSize.Height + (poolSize.Height % 2);
    if (bCaptureParamSet && !hwnd && hmon && captureParam.regionW > 1 &&
        captureParam.regionH > 1) {
      MONITORINFOEXW mi = {};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(hmon, &mi)) {
        int32_t mx = mi.rcMonitor.left;
        int32_t my = mi.rcMonitor.top;
        int32_t rx = std::max(captureParam.regionX, mx);
        int32_t ry = std::max(captureParam.regionY, my);
        int32_t rr =
            std::min(captureParam.regionX + captureParam.regionW,
                     (int32_t)mi.rcMonitor.right);
        int32_t rb =
            std::min(captureParam.regionY + captureParam.regionH,
                     (int32_t)mi.rcMonitor.bottom);
        if (rr - rx >= 2 && rb - ry >= 2) {
          srcBoxX = rx - mx;
          srcBoxY = ry - my;
          absX = rx;
          absY = ry;
          // 向下偶对齐: 不越过显示器右/下边界, 奇数尾列不采
          alignedW = (rr - rx) & ~1;
          alignedH = (rb - ry) & ~1;
          bRegion = true;
        }
      }
      if (!bRegion) {
        AVOX_WIN_LOG(E_FAIL, "winrt capture region invalid");
        dispatch(&IVideoSourceOb::onVideoError, AVError::sourceNoVaild,
                 "capture region invalid");
        return;
      }
    }
    // frameFormat跟踪真实Content尺寸(首帧通常等于item.Size, 跳过Recreate); 输出纹理用对齐偶数
    frameFormat.width = poolSize.Width;
    frameFormat.height = poolSize.Height;
    frameFormat.imageType = ImageType::bgra8;
    outSharedTex = std::make_unique<Dx11SharedTex>();
    Dx11Texture* outTexture = outSharedTex->getDx11Texture();
    outTexture->setTextureSize(alignedW, alignedH,
                               DXGI_FORMAT_B8G8R8A8_UNORM);
    outTexture->setGUI(true);
    outTexture->setNoView(true);
    outSharedTex->initTexture(device.Get());
    if (!outSharedTex->getDx11Texture()) {
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "create texture error");
      return;
    }
    setTexture(outSharedTex->getDx11Texture()->texture.Get());
    // 回报采集实际生效信息(会话落 session.json / 光标映射用)
    CaptureInfo capInfo = {};
    capInfo.cursorMode = cursorMode.c_str();
    if (bRegion) {
      capInfo.regionX = absX;
      capInfo.regionY = absY;
      capInfo.regionW = alignedW;
      capInfo.regionH = alignedH;
    }
    dispatch(&IVideoSourceOb::onCaptureInfo, capInfo);
  } catch (const winrt::hresult_error& e) {
    AVOX_WIN_LOG(static_cast<HRESULT>(e.code().value),
                "winrt capture open exception: ",
                winrt::to_string(e.message()));
    dispatch(&IVideoSourceOb::onVideoError, AVError::sourceNoVaild,
             "winrt capture open failed");
    return;
  } catch (...) {
    log(LogLevel::warn, "winrt capture open unknown exception");
    dispatch(&IVideoSourceOb::onVideoError, AVError::sourceNoVaild,
             "winrt capture open failed");
    return;
  }
  // 按 fps 轮询拉帧(类似 CaptureWindows 的 GDI 抓帧循环)
  // 节拍: 绝对到期(t0+tick*period), pts=排期时刻而非墙钟; tick 由时钟反推,
  // 慢帧跳拍(记 missedTicks) —— 计数器硬推会在慢帧时把整段录成快放
  const bool bStrict = bCaptureParamSet;
  // 首帧看门狗: 显示器目标打开成功但 item 不产帧(受保护内容/驱动异常),
  // 2s 无内容帧报错停录 —— 上层会话依赖它把"永远空转"变失败
  const bool bWatchFirstFrame = bStrict && !hwnd;
  double fps = (bStrict && captureParam.fps > 0) ? captureParam.fps : 30.0;
  int64_t fpsTick = getFrameTick(fps);
  int64_t t0 = timeTick();
  int64_t tick = 0;
  bool bContentInit = false;
  int32_t realW = 0;
  int32_t realH = 0;
#ifdef WIN32
  // 15.6ms 系统定时器粒度会让高帧率节拍发散; 1ms 粒度下绝对到期才准
  timeBeginPeriod(1);
#endif
  while (running()) {
    int64_t due = t0 + tick * fpsTick;
    while (running() && timeTick() < due) {
      sleepToTick(due);
    }
    if (!running()) {
      break;
    }
    // sleepToTick 粗睡到差 1ms, 最后 1ms 自旋到点
    while (timeTick() < due) {
    }
    // 排期时刻即 pts(严格路); 先取 pts 再推进 tick
    int64_t framePts = (int64_t)((double)tick * 1000.0 / fps + 0.5);
    int64_t prevTick = tick;
    tick = (timeTick() - t0) / fpsTick + 1;
    if (tick > prevTick + 1) {
      missedTicks += tick - prevTick - 1;
    }
    if (bWatchFirstFrame && !bContentInit && timeTick() - t0 > 20000000LL) {
      log(LogLevel::error, "capture no first frame in 2s");
      dispatch(&IVideoSourceOb::onVideoError, AVError::sourceNoVaild,
               "no first frame from capture item");
      break;
    }
    // 仅窗口捕获校验 hwnd; 显示器捕获 hwnd 为空跳过校验
    if (hwnd && !check_window_valid(hwnd)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    try {
      winrt::Direct3D11CaptureFrame frame = framePool.TryGetNextFrame();
      if (frame) {
        winrt::SizeInt32 frameSize = frame.ContentSize();
        winrt::com_ptr<ID3D11Texture2D> frameSurface = nullptr;
        auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
        winrt::check_hresult(access->GetInterface(
            winrt::guid_of<ID3D11Texture2D>(), frameSurface.put_void()));
        int32_t contentW = frameSize.Width;
        int32_t contentH = frameSize.Height;
        if (contentW != frameFormat.width || contentH != frameFormat.height) {
          if (bContentInit && bStrict) {
            // v1 契约: 首帧后内容尺寸变化=显示设置变化, 停录(尺寸守卫会全丢帧)
            log(LogLevel::error, "display changed during capture");
            dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
                     "display changed during capture");
            break;
          }
          frameFormat.width = contentW;
          frameFormat.height = contentH;
          frameFormat.imageType = ImageType::bgra8;
          // Recreate 在本线程(framePool 创建线程), 安全; 池用真实Content尺寸
          framePool.Recreate(
              rtDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
              frameSize);
          // 输出纹理: 区域裁剪路保持区域尺寸; 全画面路跟内容走(对齐向上)
          if (!bRegion) {
            alignedW = contentW + (contentW % 2);
            alignedH = contentH + (contentH % 2);
          }
          outSharedTex = std::make_unique<Dx11SharedTex>();
          Dx11Texture* rtTexture = outSharedTex->getDx11Texture();
          rtTexture->setTextureSize(alignedW, alignedH,
                                    DXGI_FORMAT_B8G8R8A8_UNORM);
          rtTexture->setGUI(true);
          rtTexture->setNoView(true);
          outSharedTex->initTexture(device.Get());
          if (outSharedTex->getDx11Texture()) {
            setTexture(outSharedTex->getDx11Texture()->texture.Get());
          }
        }
        realW = contentW;
        realH = contentH;
        bContentInit = true;
        if (outSharedTex && outSharedTex->getDx11Texture()) {
          ID3D11Texture2D* gdiTexture =
              outSharedTex->getDx11Texture()->texture.Get();
          // 区域路拷子框(按内容钳位); 全画面路拷整帧, 目标多出的边留黑
          D3D11_BOX srcBox = {};
          if (bStrict && bRegion) {
            srcBox.left =
                std::min<uint32_t>((uint32_t)srcBoxX, (uint32_t)contentW);
            srcBox.top =
                std::min<uint32_t>((uint32_t)srcBoxY, (uint32_t)contentH);
            srcBox.right = std::min<uint32_t>(srcBox.left + (uint32_t)alignedW,
                                              (uint32_t)contentW);
            srcBox.bottom = std::min<uint32_t>(srcBox.top + (uint32_t)alignedH,
                                               (uint32_t)contentH);
          } else {
            srcBox.right = (uint32_t)realW;
            srcBox.bottom = (uint32_t)realH;
          }
          context->CopySubresourceRegion(gdiTexture, 0, 0, 0, 0,
                                         frameSurface.get(), 0, &srcBox);
          outSharedTex->signalFence();
          GpuFrame gpuFrame = {};
          gpuFrame.buffer = gdiTexture;
          gpuFrame.format.width = alignedW;
          gpuFrame.format.height = alignedH;
          gpuFrame.context = outSharedTex.get();
          gpuFrame.pts = bStrict ? framePts : timeStampMS();
          gpuFrame.dts = gpuFrame.pts;
          onFrame(gpuFrame);
        }
      } else if (bStrict && bContentInit) {
        // 无新内容(CFR 补帧): 重发同一张纹理, 不拷贝(内容重复, 无写方竞争)
        if (outSharedTex && outSharedTex->getDx11Texture()) {
          GpuFrame gpuFrame = {};
          gpuFrame.buffer = outSharedTex->getDx11Texture()->texture.Get();
          gpuFrame.format.width = alignedW;
          gpuFrame.format.height = alignedH;
          gpuFrame.context = outSharedTex.get();
          gpuFrame.pts = framePts;
          gpuFrame.dts = framePts;
          onFrame(gpuFrame);
        }
      }
    } catch (const winrt::hresult_error& e) {
      AVOX_WIN_LOG(static_cast<HRESULT>(e.code().value),
                  "winrt capture frame error: ",
                  winrt::to_string(e.message()));
    }
  }
#ifdef WIN32
  timeEndPeriod(1);
#endif
  // 循环退出(stopTask join 前), 本线程清理
  try {
    if (session) {
      session.Close();
      session = nullptr;
    }
    if (framePool) {
      framePool.Close();
      framePool = nullptr;
    }
  } catch (const winrt::hresult_error& e) {
    AVOX_WIN_LOG(static_cast<HRESULT>(e.code().value),
                "winrt capture close exception: ",
                winrt::to_string(e.message()));
  }
  rtDevice = nullptr;
}

struct MonitorEnumCtx {
  CaptureWindowsMgr* mgr;
  int32_t index;
  std::vector<std::shared_ptr<WinCaptureBase>>* out = nullptr;
};

BOOL CALLBACK findWindowFunc(HWND hwnd, LPARAM l_param) {
  // 枚举用宽松判定 (含最小化窗); 捕获循环仍用 check_window_valid (shotWindow 会先恢复)
  if (isEnumerableWindow(hwnd)) {
    MonitorEnumCtx* ctx = reinterpret_cast<MonitorEnumCtx*>(l_param);
    ctx->mgr->addWindow(hwnd, *ctx->out);
  }
  return true;
}

BOOL CALLBACK monitorEnumProc(HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
  auto* ctx = reinterpret_cast<MonitorEnumCtx*>(lparam);
  ctx->mgr->addMonitor(hmon, ctx->index, *ctx->out);
  ctx->index++;
  return TRUE;
}

CaptureWindowsMgr::CaptureWindowsMgr() {
  sdkType = VDeviceSdk::win_capture;
  bRT = isWinRtCaptureAvailable();
  // GDI 回退道测试开关: AVOX_CAPTURE_GDI=1 强制走 BitBlt 抓帧器
  if (std::getenv("AVOX_CAPTURE_GDI") != nullptr) {
    bRT = false;
  }
  LOGFLF(LogLevel::info, "winRT:", bRT);
  onRefreshDevices();
}

void CaptureWindowsMgr::onRefreshDevices() {
  // 清空重建; 打开中/被会话占用的源对象 pin 住原样保留(替换会让持有裸指针的
  // 会话悬垂), 与 pin 源同目标的重复枚举条目跳过
  std::vector<std::shared_ptr<WinCaptureBase>> fresh;
  MonitorEnumCtx ctx = {this, 0, &fresh};
  ::EnumDisplayMonitors(NULL, NULL, monitorEnumProc,
                        reinterpret_cast<LPARAM>(&ctx));
  ::EnumWindows(findWindowFunc, reinterpret_cast<LPARAM>(&ctx));
  std::vector<std::shared_ptr<WinCaptureBase>> kept;
  for (auto& dev : devices) {
    if (!dev) {
      continue;
    }
    if (!dev->bOpening() && !isVideoSourceBusy(dev.get())) {
      continue;
    }
    fresh.erase(std::remove_if(fresh.begin(), fresh.end(),
                               [&](const std::shared_ptr<WinCaptureBase>& d) {
                                 return d->nativeHwnd() == dev->nativeHwnd() &&
                                        d->nativeHmon() == dev->nativeHmon();
                               }),
                fresh.end());
    kept.push_back(dev);
  }
  fresh.insert(fresh.end(), kept.begin(), kept.end());
  devices.swap(fresh);
}

void CaptureWindowsMgr::addWindow(
    HWND hwnd, std::vector<std::shared_ptr<WinCaptureBase>>& out) {
  DevicePtr windowPtr = bRT ? DevicePtr(std::make_shared<RTCaptureWindows>())
                            : DevicePtr(std::make_shared<CaptureWindows>());
  windowPtr->setWindow(hwnd);
  out.push_back(windowPtr);
}

void CaptureWindowsMgr::addMonitor(
    HMONITOR hmon, int32_t index,
    std::vector<std::shared_ptr<WinCaptureBase>>& out) {
  DevicePtr windowPtr = bRT ? DevicePtr(std::make_shared<RTCaptureWindows>())
                            : DevicePtr(std::make_shared<CaptureWindows>());
  windowPtr->setMonitor(hmon, index);
  out.push_back(windowPtr);
}

}
