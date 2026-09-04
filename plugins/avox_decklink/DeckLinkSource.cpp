#include "DeckLinkSource.hpp"

#include "avox/module/HighClock.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

DeckLinkSource::~DeckLinkSource() { close(); }

bool DeckLinkSource::init(IDeckLink* deckLink_) {
  deckLink = deckLink_;
  if (!deckLink ||
      FAILED(deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&input))) {
    return false;
  }
  deviceKind = VDeviceKind::camera;
  taskName = "decklink input";
  // GetDisplayName/GetModelName返回BSTR, 用SysFreeString释放
  wchar_t* nameStr = nullptr;
  wchar_t* idStr = nullptr;
  deckLink->GetDisplayName(&nameStr);
  deckLink->GetModelName(&idStr);
  deviceName = nameStr ? utf8TString(std::wstring(nameStr)) : "decklink";
  deviceId = idStr ? utf8TString(std::wstring(idStr)) : deviceName;
  SysFreeString(nameStr);
  SysFreeString(idStr);
  // 拼拓扑Id, 多卡同型号时Id仍唯一 (11.6 SDK拓扑Id在ProfileAttributes里)
  MComPtr<IDeckLinkProfileAttributes> profile = nullptr;
  int64_t topologicalId = 0;
  if (SUCCEEDED(deckLink->QueryInterface(IID_IDeckLinkProfileAttributes,
                                         (void**)&profile))) {
    profile->GetInt(BMDDeckLinkTopologicalID, &topologicalId);
    // 是否支持输入信号格式自适应
    if (profile->GetFlag(BMDDeckLinkSupportsInputFormatDetection,
                         &bFormatDetection) != S_OK) {
      bFormatDetection = FALSE;
    }
  }
  deviceId += "-" + std::to_string(topologicalId);
  // 枚举display mode建格式列表(输出像素固定bmdFormat8BitYUV→uyvyI)
  MComPtr<IDeckLinkDisplayModeIterator> modeIterator = nullptr;
  if (FAILED(input->GetDisplayModeIterator(&modeIterator))) {
    return false;
  }
  MComPtr<IDeckLinkDisplayMode> mode = nullptr;
  while (modeIterator->Next(&mode) == S_OK) {
    long long frameDuration = 0;
    long long timeScale = 0;
    mode->GetFrameRate(&frameDuration, &timeScale);
    VideoDesc desc = {};
    desc.width = mode->GetWidth();
    desc.height = mode->GetHeight();
    desc.fps = frameDuration > 0 ? (double)timeScale / frameDuration : 0;
    desc.type = YuvType::uyvyI;
    descList.push_back(desc);
    modeList.push_back(mode);
    mode.Reset();
  }
  if (descList.empty()) {
    return false;
  }
  setVideoDesc(1920, 1080, 60);
  input->SetCallback(this);
  LOGFLF(LogLevel::info, "decklink init:", deviceName,
         " detection:", bFormatDetection, " formats:", descList.size());
  return true;
}

int32_t DeckLinkSource::findModeIndex() {
  int32_t best = 0;
  int64_t bestScore = INT64_MAX;
  for (int32_t i = 0; i < (int32_t)modeList.size(); i++) {
    const VideoDesc& desc = descList[i];
    int64_t score = abs(desc.width - curDesc.width) +
                    abs(desc.height - curDesc.height) +
                    (int64_t)abs((int)(desc.fps - curDesc.fps)) * 10;
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

bool DeckLinkSource::onOpen() {
  std::lock_guard<std::mutex> locker(playMtx);
  if (!input || modeList.empty()) {
    return false;
  }
  // 支持格式自适应时驱动自动跟随实际输入信号制式
  BMDVideoInputFlags flags = bmdVideoInputFlagDefault;
  if (bFormatDetection) {
    flags |= bmdVideoInputEnableFormatDetection;
  }
  selectIndex = findModeIndex();
  MComPtr<IDeckLinkDisplayMode>& mode = modeList[selectIndex];
  // 先停流再使能, 不然切模式不生效
  input->StopStreams();
  if (input->EnableVideoInput(mode->GetDisplayMode(), bmdFormat8BitYUV,
                              flags) != S_OK) {
    LOGFLF(LogLevel::warn, "decklink enable video input failed, device:",
           deviceName);
    return false;
  }
  startTask();
  if (input->StartStreams() != S_OK) {
    LOGFLF(LogLevel::warn, "decklink start streams failed, device:",
           deviceName);
    return false;
  }
  bOpen = true;
  LOGFLF(LogLevel::info, "decklink start reading, device:", deviceName);
  return true;
}

void DeckLinkSource::onClose() {
  std::lock_guard<std::mutex> locker(playMtx);
  bOpen = false;
  stopTask();
  if (input) {
    input->StopStreams();
    input->DisableVideoInput();
  }
}

bool DeckLinkSource::bOpening() { return bOpen; }

void DeckLinkSource::onRunTask() {
  while (running()) {
    std::unique_lock<std::mutex> locker(frameMtx);
    // 等DeckLink回调线程送帧, 200ms超时轮询退出标志
    frameCv.wait_for(locker, std::chrono::milliseconds(200),
                     [this] { return !frameQueue.empty() || !running(); });
    if (!running()) {
      break;
    }
    if (frameQueue.empty()) {
      continue;
    }
    FrameItem item = std::move(frameQueue.front());
    frameQueue.pop_front();
    locker.unlock();
    YUVFrame frame = {};
    frame.pts = timeStampMS();
    frame.dts = frame.pts;
    frame.data[0] = item.data.data();
    frame.stride[0] = item.stride;
    frame.format = {item.width, item.height, YuvType::uyvyI};
    onFrame(frame);
  }
}

void DeckLinkSource::pushFrame(const uint8_t* data, int32_t size, int32_t width,
                               int32_t height, int32_t stride) {
  std::lock_guard<std::mutex> locker(frameMtx);
  // 上限3帧丢最旧, 防止下游卡顿内存累积
  while (frameQueue.size() >= 3) {
    frameQueue.pop_front();
  }
  FrameItem item = {};
  item.data.assign(data, data + size);
  item.width = width;
  item.height = height;
  item.stride = stride;
  frameQueue.push_back(std::move(item));
  frameCv.notify_one();
}

void DeckLinkSource::lostDevice() {
  dispatch(&IVideoSourceOb::onVideoError, AVError::devcieLost, "device lost");
}

HRESULT DeckLinkSource::QueryInterface(REFIID iid, LPVOID* ppv) {
  if (ppv == nullptr) {
    return E_POINTER;
  }
  *ppv = nullptr;
  // SetCallback直传接口指针, 只需应答IUnknown
  if (memcmp(&iid, &IID_IUnknown, sizeof(REFIID)) == 0) {
    *ppv = this;
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

ULONG DeckLinkSource::AddRef() { return InterlockedIncrement(&refCount); }

ULONG DeckLinkSource::Release() { return InterlockedDecrement(&refCount); }

HRESULT DeckLinkSource::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame* videoFrame,
    IDeckLinkAudioInputPacket* audioPacket) {
  // 人为停流中断
  if (!bOpen || !videoFrame) {
    return S_OK;
  }
  uint8_t* data = nullptr;
  if (videoFrame->GetBytes((void**)&data) != S_OK || !data) {
    return S_OK;
  }
  int32_t stride = (int32_t)videoFrame->GetRowBytes();
  int32_t size = stride * (int32_t)videoFrame->GetHeight();
  pushFrame(data, size, (int32_t)videoFrame->GetWidth(),
            (int32_t)videoFrame->GetHeight(), stride);
  return S_OK;
}

HRESULT DeckLinkSource::VideoInputFormatChanged(
    BMDVideoInputFormatChangedEvents notificationEvents,
    IDeckLinkDisplayMode* newDisplayMode,
    BMDDetectedVideoInputFormatFlags detectedSignalFlags) {
  if (!(notificationEvents & bmdVideoInputDisplayModeChanged)) {
    return S_OK;
  }
  std::lock_guard<std::mutex> locker(playMtx);
  // 关流竞态: close后不再以新格式重启
  if (!bOpen || !input) {
    return S_OK;
  }
  // 输出固定8BitYUV(RGB信号由驱动转换), 新带宽高由checkSizeChanged自动更新desc
  input->PauseStreams();
  HRESULT hr =
      input->EnableVideoInput(newDisplayMode->GetDisplayMode(),
                              bmdFormat8BitYUV,
                              bmdVideoInputEnableFormatDetection);
  if (hr != S_OK) {
    LOGFLF(LogLevel::warn, "decklink re-enable input failed, device:",
           deviceName);
    input->StopStreams();
    bOpen = false;
    lostDevice();
    return E_FAIL;
  }
  // 清理旧制式缓冲后重启
  input->FlushStreams();
  input->StartStreams();
  return S_OK;
}

DeckLinkSourceMgr::DeckLinkSourceMgr() {
  // DeckLink API要求调用线程COM已初始化, S_FALSE也算成功
  coInited = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  onInitDevices();
}

DeckLinkSourceMgr::~DeckLinkSourceMgr() {
  // 设备析构会StopStreams, 需在CoUninitialize前完成
  devices.clear();
  if (coInited) {
    CoUninitialize();
  }
}

void DeckLinkSourceMgr::onInitDevices() {
  devices.clear();
  MComPtr<IDeckLinkIterator> iterator = nullptr;
  if (FAILED(CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
                              IID_IDeckLinkIterator, (void**)&iterator))) {
    LOGFLF(LogLevel::warn, "decklink drivers not installed, device list empty");
    return;
  }
  MComPtr<IDeckLinkAPIInformation> apiInfo = nullptr;
  if (SUCCEEDED(iterator->QueryInterface(IID_IDeckLinkAPIInformation,
                                         (void**)&apiInfo))) {
    wchar_t* versionStr = nullptr;
    if (apiInfo->GetString(BMDDeckLinkAPIVersion, &versionStr) == S_OK) {
      LOGFLF(LogLevel::info, "decklink sdk:", BLACKMAGIC_DECKLINK_API_VERSION_STRING,
             " driver:", utf8TString(std::wstring(versionStr)));
      SysFreeString(versionStr);
    }
  }
  IDeckLink* deckLink = nullptr;
  while (iterator->Next(&deckLink) == S_OK) {
    std::shared_ptr<DeckLinkSource> device = std::make_shared<DeckLinkSource>();
    if (device->init(deckLink)) {
      devices.push_back(device);
    }
    deckLink->Release();
  }
  LOGFLF(LogLevel::info, "decklink device count:", devices.size());
}

}  // namespace avox
