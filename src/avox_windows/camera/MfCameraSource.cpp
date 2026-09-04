#include "MfCameraSource.hpp"

#include <shlwapi.h>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"

namespace avox {

void regWinMfCameraDevice() {
  RegFunc regFunc = {"win mf camera device init", []() {
                       AvoxManager::Get().vDeviceMgr.regMgrObj(
                           VDeviceSdk::win_mf, []() -> IVideoManager* {
                             return new MfCameraSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

MfCameraSource::~MfCameraSource() { close(); }

bool MfCameraSource::init(IMFActivate* activate_) {
  activate = activate_;
  deviceKind = VDeviceKind::camera;
  taskName = "win mf camera";
  wchar_t* nameStr = nullptr;
  wchar_t* idStr = nullptr;
  activate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameStr,
                               nullptr);
  activate->GetAllocatedString(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &idStr,
      nullptr);
  deviceName = nameStr ? utf8TString(std::wstring(nameStr)) : "mf camera";
  deviceId = idStr ? utf8TString(std::wstring(idStr)) : deviceName;
  CoTaskMemFree(nameStr);
  CoTaskMemFree(idStr);
  if (!ensureReader()) {
    return false;
  }
  // 很多采集卡能激活但MF读不了, 格式枚举失败直接跳过, 不给上层报错(aoce同)
  if (!getMfCameraFormats(source.Get(), mfFormats, handler)) {
    return false;
  }
  for (const auto& format : mfFormats) {
    descList.push_back(format.desc);
  }
  setVideoDesc(1920, 1080, 30);
  LOGFLF(LogLevel::info, "mf camera init:", deviceName,
         " formats:", descList.size());
  return true;
}

bool MfCameraSource::ensureReader() {
  if (source) {
    return true;
  }
  if (!readerAttrs) {
    if (FAILED(MFCreateAttributes(&readerAttrs, 2))) {
      return false;
    }
    readerAttrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    readerAttrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
  }
  AVOX_WIN_LOG_RETURN_FALSE(
      activate->ActivateObject(__uuidof(IMFMediaSource), (void**)&source),
      "mf camera create media source failed");
  AVOX_WIN_LOG_RETURN_FALSE(
      MFCreateSourceReaderFromMediaSource(source.Get(), readerAttrs.Get(),
                                          &sourceReader),
      "mf camera create source reader failed");
  return true;
}

int32_t MfCameraSource::findFormatIndex() {
  int32_t best = 0;
  int64_t bestScore = INT64_MAX;
  for (int32_t i = 0; i < (int32_t)mfFormats.size(); i++) {
    const VideoDesc& desc = mfFormats[i].desc;
    int64_t score = abs(desc.width - curDesc.width) +
                    abs(desc.height - curDesc.height) +
                    (int64_t)abs((int)(desc.fps - curDesc.fps)) * 10;
    if (desc.type != curDesc.type) {
      score += 100000;
    }
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

bool MfCameraSource::onOpen() {
  std::lock_guard<std::mutex> locker(playMtx);
  if (mfFormats.empty()) {
    return false;
  }
  // close时已释放source, 这里按需重建
  if (!ensureReader()) {
    return false;
  }
  // 先停在读的流, 不然设置不了格式
  setPlay(false);
  selectIndex = findFormatIndex();
  const MfCameraFormat& format = mfFormats[selectIndex];
  MComPtr<IMFMediaType> mtype = nullptr;
  if (FAILED(handler->GetMediaTypeByIndex(format.mfIndex, &mtype))) {
    LOGFLF(LogLevel::warn, "mf camera get media type failed, device:",
           deviceName);
    return false;
  }
  // source应用新格式
  if (FAILED(handler->SetCurrentMediaType(mtype.Get()))) {
    LOGFLF(LogLevel::warn, "mf camera set source format failed, device:",
           deviceName);
    return false;
  }
  // reader取当前原生格式, 压缩格式(MJPG)改写subtype由内置解码器转码
  MComPtr<IMFMediaType> pType = nullptr;
  if (FAILED(
          sourceReader->GetNativeMediaType(videoIndex, format.mfIndex, &pType))) {
    LOGFLF(LogLevel::warn, "mf camera get native media type failed, device:",
           deviceName);
    return false;
  }
  GUID subtype = {};
  pType->GetGUID(MF_MT_SUBTYPE, &subtype);
  if (subtype == MFVideoFormat_MJPG) {
    pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
  }
  if (FAILED(sourceReader->SetCurrentMediaType(videoIndex, nullptr,
                                               pType.Get()))) {
    LOGFLF(LogLevel::warn, "mf camera set reader format failed, device:",
           deviceName);
    return false;
  }
  // 输出行距, 读不到按默认算(NV12一字节每像素, YUY2类两字节每像素)
  frameStride = 0;
  MComPtr<IMFMediaType> curType = nullptr;
  UINT32 stride = 0;
  if (SUCCEEDED(sourceReader->GetCurrentMediaType(videoIndex, &curType)) &&
      SUCCEEDED(curType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
    frameStride = (int32_t)stride;
  }
  if (frameStride <= 0) {
    frameStride = curDesc.type == YuvType::nv12 ? curDesc.width
                                                : curDesc.width * 2;
  }
  startTask();
  return setPlay(true);
}

void MfCameraSource::onClose() {
  std::lock_guard<std::mutex> locker(playMtx);
  stopTask();
  setPlay(false);
  if (source) {
    // 显式释放reader与source, 不依赖MF回调引用计数(生命周期归Mgr的shared_ptr)
    sourceReader.Reset();
    source.Reset();
    activate->ShutdownObject();
  }
}

bool MfCameraSource::bOpening() { return bOpen; }

bool MfCameraSource::setPlay(bool play) {
  if (play) {
    if (bOpen) {
      return true;
    }
    int32_t retry = 0;
    HRESULT hr = sourceReader->ReadSample(videoIndex, 0, nullptr, nullptr,
                                          nullptr, nullptr);
    // 最多重试三次
    while (FAILED(hr) && retry++ < 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      hr = sourceReader->ReadSample(videoIndex, 0, nullptr, nullptr, nullptr,
                                    nullptr);
    }
    if (SUCCEEDED(hr)) {
      bOpen = true;
      LOGFLF(LogLevel::info, "mf camera start reading, device:", deviceName);
      return true;
    }
    AVOX_WIN_LOG(hr, "mf camera open failed, device:", deviceName);
    return false;
  }
  if (!bOpen) {
    return true;
  }
  bOpen = false;
  // 清空排队旧帧, 避免重开时推出过期画面
  {
    std::lock_guard<std::mutex> locker(frameMtx);
    frameQueue.clear();
  }
  if (sourceReader) {
    HRESULT hr = sourceReader->Flush(videoIndex);
    if (SUCCEEDED(hr)) {
      std::unique_lock<std::mutex> locker(flushMtx);
      auto status = flushSignal.wait_for(locker, std::chrono::seconds(3));
      if (status == std::cv_status::timeout) {
        LOGFLF(LogLevel::info, "mf camera flush timeout, device:", deviceName);
      }
    }
  }
  return true;
}

void MfCameraSource::onRunTask() {
  while (running()) {
    std::unique_lock<std::mutex> locker(frameMtx);
    // 等MF回调线程送帧, 200ms超时轮询退出标志
    frameCv.wait_for(locker, std::chrono::milliseconds(200),
                     [this] { return !frameQueue.empty() || !running(); });
    if (!running()) {
      break;
    }
    if (frameQueue.empty()) {
      continue;
    }
    std::vector<uint8_t> data = std::move(frameQueue.front());
    frameQueue.pop_front();
    locker.unlock();
    YUVFrame frame = {};
    frame.pts = timeStampMS();
    frame.dts = frame.pts;
    frame.data[0] = data.data();
    frame.stride[0] = frameStride;
    frame.format = {curDesc.width, curDesc.height, curDesc.type};
    if (curDesc.type == YuvType::nv12) {
      // NV12: UV平面紧跟Y平面, 行距相同
      frame.data[1] = frame.data[0] + (size_t)frameStride * curDesc.height;
      frame.stride[1] = frameStride;
    }
    onFrame(frame);
  }
}

void MfCameraSource::pushFrame(const uint8_t* data, int32_t size) {
  std::lock_guard<std::mutex> locker(frameMtx);
  // 上限3帧丢最旧, 防止下游卡顿内存累积
  while (frameQueue.size() >= 3) {
    frameQueue.pop_front();
  }
  frameQueue.emplace_back(data, data + size);
  frameCv.notify_one();
}

void MfCameraSource::lostDevice() {
  dispatch(&IVideoSourceOb::onVideoError, AVError::devcieLost, "device lost");
}

HRESULT MfCameraSource::QueryInterface(REFIID riid, void** ppvObject) {
  static const QITAB qit[] = {
      QITABENT(MfCameraSource, IMFSourceReaderCallback),
      {0},
  };
  return QISearch(this, qit, riid, ppvObject);
}

ULONG MfCameraSource::AddRef() { return InterlockedIncrement(&refCount); }

ULONG MfCameraSource::Release() { return InterlockedDecrement(&refCount); }

HRESULT MfCameraSource::OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                                     DWORD dwStreamFlags,
                                     LONGLONG llTimestamp, IMFSample* pSample) {
  // 人为停流中断, 不再请求下一帧
  if (!bOpen) {
    return S_OK;
  }
  if (FAILED(hrStatus)) {
    bOpen = false;
    LOGFLF(LogLevel::warn, "mf camera data interruption, device:", deviceName);
    lostDevice();
    return hrStatus;
  }
  if (pSample) {
    MComPtr<IMFMediaBuffer> buffer = nullptr;
    if (SUCCEEDED(pSample->GetBufferByIndex(0, &buffer)) && buffer) {
      BYTE* data = nullptr;
      DWORD maxLength = 0;
      DWORD length = 0;
      if (SUCCEEDED(buffer->Lock(&data, &maxLength, &length))) {
        pushFrame(data, (int32_t)length);
        buffer->Unlock();
      }
    }
  }
  // 请求下一帧
  HRESULT hr = sourceReader->ReadSample(videoIndex, 0, nullptr, nullptr,
                                        nullptr, nullptr);
  if (FAILED(hr)) {
    bOpen = false;
    LOGFLF(LogLevel::warn, "mf camera data interruption, device:", deviceName);
    lostDevice();
  }
  return hr;
}

HRESULT MfCameraSource::OnFlush(DWORD dwStreamIndex) {
  flushSignal.notify_all();
  return S_OK;
}

HRESULT MfCameraSource::OnEvent(DWORD dwStreamIndex, IMFMediaEvent* pEvent) {
  return S_OK;
}

MfCameraSourceMgr::MfCameraSourceMgr() {
  // MFStartup引用计数, 析构配对MFShutdown
  mfStarted = SUCCEEDED(MFStartup(MF_VERSION));
  if (!mfStarted) {
    LOGFLF(LogLevel::warn, "mf camera MFStartup failed");
    return;
  }
  onInitDevices();
}

MfCameraSourceMgr::~MfCameraSourceMgr() {
  // 设备析构会关流并释放MF对象, 需在MFShutdown前完成
  devices.clear();
  if (mfStarted) {
    MFShutdown();
  }
}

void MfCameraSourceMgr::onInitDevices() {
  devices.clear();
  MComPtr<IMFAttributes> attrs = nullptr;
  if (FAILED(MFCreateAttributes(&attrs, 1))) {
    return;
  }
  attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** ppDevices = nullptr;
  UINT32 count = 0;
  if (FAILED(MFEnumDeviceSources(attrs.Get(), &ppDevices, &count))) {
    return;
  }
  for (UINT32 i = 0; i < count; i++) {
    std::shared_ptr<MfCameraSource> device = std::make_shared<MfCameraSource>();
    if (device->init(ppDevices[i])) {
      devices.push_back(device);
    }
    ppDevices[i]->Release();
  }
  CoTaskMemFree(ppDevices);
  LOGFLF(LogLevel::info, "mf camera device count:", devices.size());
}

}  // namespace avox
