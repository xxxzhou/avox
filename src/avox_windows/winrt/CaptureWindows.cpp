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
  double fps = 30.0;
  int64_t fpsTick = getFrameTick(fps);
  while (running()) {
    int64_t tempTick = timeTick();
    int32_t width = 0;
    int32_t height = 0;
    int32_t srcX = 0;
    int32_t srcY = 0;
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
    gpuFrame.pts = timeStampMS();
    gpuFrame.dts = gpuFrame.pts;
    // checkSizeChanged(gpuFrame.format);
    onFrame(gpuFrame);
    // 计算休眠时间
    int64_t sleepTick = fpsTick - (timeTick() - tempTick);
    if (sleepTick > 20000) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(sleepTick / 10000LL));
    }
  }
}

RTCaptureWindows::RTCaptureWindows() {}

RTCaptureWindows::~RTCaptureWindows() {}

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
    // 池尺寸保持WinRT真实尺寸; 仅输出纹理对齐到偶数, 避免奇数宽高下游YUV420P/编码失败
    int32_t alignedW = poolSize.Width + (poolSize.Width % 2);
    int32_t alignedH = poolSize.Height + (poolSize.Height % 2);
    framePool = winrt::Direct3D11CaptureFramePool::Create(
        rtDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
        poolSize);
    session = framePool.CreateCaptureSession(item);
    // 不注册 FrameArrived, 改为下方按 fps 轮询 TryGetNextFrame
    session.StartCapture();
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
  double fps = 30.0;
  int64_t fpsTick = getFrameTick(fps);
  while (running()) {
    int64_t tempTick = timeTick();
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
        // 源帧内容尺寸(WinRT ContentSize, 可能为奇数)
        int32_t realW = frameSize.Width;
        int32_t realH = frameSize.Height;
        // 输出对齐到偶数: 下游YUV420P/编码要求(奇数会绿边/编码失败); 池仍用真实Content尺寸
        int32_t width = realW + (realW % 2);
        int32_t height = realH + (realH % 2);
        if (realW > 0 && realH > 0) {
          if (realW != frameFormat.width || realH != frameFormat.height) {
            frameFormat.width = realW;
            frameFormat.height = realH;
            frameFormat.imageType = ImageType::bgra8;
            // Recreate 在本线程(framePool 创建线程), 安全; 池用真实Content尺寸
            framePool.Recreate(
                rtDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                frameSize);
            outSharedTex = std::make_unique<Dx11SharedTex>();
            Dx11Texture* rtTexture = outSharedTex->getDx11Texture();
            rtTexture->setTextureSize(width, height,
                                      DXGI_FORMAT_B8G8R8A8_UNORM);
            rtTexture->setGUI(true);
            rtTexture->setNoView(true);
            outSharedTex->initTexture(device.Get());
            if (outSharedTex->getDx11Texture()) {
              setTexture(outSharedTex->getDx11Texture()->texture.Get());
            }
          }
          if (outSharedTex && outSharedTex->getDx11Texture()) {
            ID3D11Texture2D* gdiTexture =
                outSharedTex->getDx11Texture()->texture.Get();
            // 源帧按真实内容尺寸(realW x realH)拷到对齐后的目标, 目标多出的最右/最下边缘
            // 像素不被覆盖(留黑), 与GDI路径BitBlt越界同理; 用Subregion避免源/目标尺寸失配
            D3D11_BOX srcBox = {0, 0, 0, static_cast<UINT>(realW),
                                static_cast<UINT>(realH), 1};
            context->CopySubresourceRegion(gdiTexture, 0, 0, 0, 0,
                                           frameSurface.get(), 0, &srcBox);
            outSharedTex->signalFence();
            GpuFrame gpuFrame = {};
            gpuFrame.buffer = gdiTexture;
            gpuFrame.format.width = width;
            gpuFrame.format.height = height;
            gpuFrame.context = outSharedTex.get();
            gpuFrame.pts = timeStampMS();
            gpuFrame.dts = gpuFrame.pts;
            onFrame(gpuFrame);
          }
        }
      }
    } catch (const winrt::hresult_error& e) {
      AVOX_WIN_LOG(static_cast<HRESULT>(e.code().value),
                  "winrt capture frame error: ",
                  winrt::to_string(e.message()));
    }
    int64_t sleepTick = fpsTick - (timeTick() - tempTick);
    if (sleepTick > 20000) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(sleepTick / 10000LL));
    }
  }
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

BOOL CALLBACK findWindowFunc(HWND hwnd, LPARAM l_param) {
  // 枚举用宽松判定 (含最小化窗); 捕获循环仍用 check_window_valid (shotWindow 会先恢复)
  if (isEnumerableWindow(hwnd)) {
    CaptureWindowsMgr* mwManager =
        reinterpret_cast<CaptureWindowsMgr*>(l_param);
    mwManager->addWindow(hwnd);
  }
  return true;
}

struct MonitorEnumCtx {
  CaptureWindowsMgr* mgr;
  int32_t index;
};

BOOL CALLBACK monitorEnumProc(HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
  auto* ctx = reinterpret_cast<MonitorEnumCtx*>(lparam);
  ctx->mgr->addMonitor(hmon, ctx->index);
  ctx->index++;
  return TRUE;
}

CaptureWindowsMgr::CaptureWindowsMgr() {
  sdkType = VDeviceSdk::win_capture;
  bRT = isWinRtCaptureAvailable();
  LOGFLF(LogLevel::info, "winRT:", bRT);
  onRefreshDevices();
}

void CaptureWindowsMgr::onRefreshDevices() {
  MonitorEnumCtx ctx = {this, 0};
  ::EnumDisplayMonitors(NULL, NULL, monitorEnumProc,
                        reinterpret_cast<LPARAM>(&ctx));
  ::EnumWindows(findWindowFunc, reinterpret_cast<LPARAM>(this));
}

void CaptureWindowsMgr::addWindow(HWND hwnd) {
  DevicePtr windowPtr = bRT ? DevicePtr(std::make_shared<RTCaptureWindows>())
                            : DevicePtr(std::make_shared<CaptureWindows>());
  windowPtr->setWindow(hwnd);
  devices.push_back(windowPtr);
}

void CaptureWindowsMgr::addMonitor(HMONITOR hmon, int32_t index) {
  DevicePtr windowPtr = bRT ? DevicePtr(std::make_shared<RTCaptureWindows>())
                            : DevicePtr(std::make_shared<CaptureWindows>());
  windowPtr->setMonitor(hmon, index);
  devices.push_back(windowPtr);
}

}
