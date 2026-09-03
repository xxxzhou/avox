#include "WasAudioSource.hpp"

#include "WasAudioRender.hpp"
#include "avox/AvoxAudio.h"
#include "avox/module/AvoxManager.hpp"

#ifndef PKEY_Device_FriendlyName
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,
                   0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
#endif

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace avox {

void regWasAudioDevice() {
  RegFunc regFunc = {"WASAPI audio device init", []() {
                       AvoxManager::Get().aDeviceMgr.regMgrObj(
                           ADeviceSdk::wasapi, []() -> IAudioManager* {
                             return new WasAudioSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

WasAudioBase::WasAudioBase(MComPtr<IMMDevice> device, ADeviceKind kind) {
  mmDevice = device;
  deviceKind = kind;
  if (mmDevice) {
    LPWSTR pId = nullptr;
    mmDevice->GetId(&pId);
    if (pId) {
      deviceId = utf8TString(pId);
      CoTaskMemFree(pId);
    }
    IPropertyStore* pProps = nullptr;
    HRESULT hr = mmDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (SUCCEEDED(hr)) {
      PROPVARIANT varName;
      PropVariantInit(&varName);
      hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
      if (SUCCEEDED(hr)) {
        deviceName = utf8TString(varName.pwszVal);
        PropVariantClear(&varName);
      }
      pProps->Release();
    }
  }
  // loopback是render设备的回环采集, 名称加后缀与同名播放设备区分
  if (deviceKind == ADeviceKind::loopback) {
    deviceName += " [loopback]";
  }
}

WasAudioBase::~WasAudioBase() { close(); }

bool WasAudioBase::onOpen() {
  if (!mmDevice) {
    log(LogLevel::warn, "WASAPI device is null");
    return false;
  }
  HRESULT hr = mmDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  (void**)&audioClient);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to activate audio client:", hr);
    return false;
  }
  WAVEFORMATEX* pwfx = nullptr;
  hr = audioClient->GetMixFormat(&pwfx);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to get mix format:", hr);
    return false;
  }
  desc.sampleRate = pwfx->nSamplesPerSec;
  desc.channels = pwfx->nChannels;
  desc.format = waveFormatToAudioFormat(pwfx);
  hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags(), 10000000,
                               0, pwfx, nullptr);
  CoTaskMemFree(pwfx);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "failed to initialize audio client");
    return false;
  }
  hr = audioClient->GetBufferSize(&bufferFrameCount);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to get buffer size:", hr);
    return false;
  }
  log(LogLevel::info, "WASAPI open audio device:", deviceName, " desc: ", desc);
  startTask();
  return true;
}

void WasAudioBase::drainPackets(std::vector<uint8_t>& buffer,
                                uint32_t sampleSize) {
  UINT32 packetLength = 0;
  HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
  if (FAILED(hr)) {
    log(LogLevel::warn, "GetNextPacketSize failed:", hr);
    return;
  }
  while (packetLength != 0 && running()) {
    BYTE* pData = nullptr;
    UINT32 numFramesAvailable = 0;
    DWORD flags = 0;
    hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr,
                                  nullptr);
    if (FAILED(hr)) {
      log(LogLevel::warn, "GetBuffer failed:", hr);
      break;
    }
    if (numFramesAvailable > 0 && pData) {
      UINT32 bytesToRead = numFramesAvailable * sampleSize;
      if (bytesToRead <= buffer.size()) {
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
          memset(buffer.data(), 0, bytesToRead);
        } else {
          memcpy(buffer.data(), pData, bytesToRead);
        }
        AvoxAFrame frame = {};
        frame.buffer.data = buffer.data();
        frame.buffer.size = bytesToRead;
        frame.pts = timeStampMS();
        onFrame(frame);
      }
      hr = captureClient->ReleaseBuffer(numFramesAvailable);
      if (FAILED(hr)) {
        log(LogLevel::warn, "ReleaseBuffer failed:", hr);
      }
    }
    hr = captureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr)) {
      break;
    }
  }
}

void WasAudioBase::onClose() {
  stopTask();
  audioClient.Reset();
  log(LogLevel::info, "WASAPI audio device closed");
}

bool WasAudioBase::bOpening() { return running(); }

WasMicSource::WasMicSource(MComPtr<IMMDevice> device)
    : WasAudioBase(device, ADeviceKind::mic) {}

void WasMicSource::onRunTask() {
  HRESULT hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                       (void**)&captureClient);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to get capture client:", hr);
    return;
  }
  // 使用更大的缓冲区，确保能容纳WASAPI返回的数据
  int32_t bufferSize = getAudioFrameSize(desc, 100);  // 100ms缓冲区
  std::vector<uint8_t> buffer(bufferSize);
  uint32_t sampleSize = desc.channels * audioFormatSize(desc.format);
  eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!eventHandle) {
    log(LogLevel::warn, "Failed to create event handle");
    return;
  }
  hr = audioClient->SetEventHandle(eventHandle);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to set event handle:", hr);
    CloseHandle(eventHandle);
    eventHandle = nullptr;
    return;
  }
  hr = audioClient->Start();
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to start audio client:", hr);
    CloseHandle(eventHandle);
    eventHandle = nullptr;
    return;
  }
  while (running()) {
    DWORD waitResult = WaitForSingleObject(eventHandle, 2000);
    if (waitResult == WAIT_OBJECT_0) {
      drainPackets(buffer, sampleSize);
    }
  }
  // 停止音频设备
  audioClient->Stop();
  CloseHandle(eventHandle);
  eventHandle = nullptr;
  captureClient.Reset();
}

WasLoopbackSource::WasLoopbackSource(MComPtr<IMMDevice> device)
    : WasAudioBase(device, ADeviceKind::loopback) {}

void WasLoopbackSource::onRunTask() {
  HRESULT hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                       (void**)&captureClient);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to get capture client:", hr);
    return;
  }
  // 使用更大的缓冲区，确保能容纳WASAPI返回的数据
  int32_t bufferSize = getAudioFrameSize(desc, 100);  // 100ms缓冲区
  std::vector<uint8_t> buffer(bufferSize);
  uint32_t sampleSize = desc.channels * audioFormatSize(desc.format);
  hr = audioClient->Start();
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to start audio client:", hr);
    return;
  }
  // loopback不支持事件回调, 用定时轮询
  while (running()) {
    drainPackets(buffer, sampleSize);
    sleepTask(false, 10);
  }
  // 停止音频设备
  audioClient->Stop();
  captureClient.Reset();
}

WasAudioSourceMgr::WasAudioSourceMgr() {
  sdkType = ADeviceSdk::wasapi;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  onInitDevices();
}

WasAudioSourceMgr::~WasAudioSourceMgr() { CoUninitialize(); }

void WasAudioSourceMgr::onInitDevices() {
  devices.clear();
  MComPtr<IMMDeviceEnumerator> pEnumerator;
  HRESULT hr =
      CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                       __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to create device enumerator:", hr);
    return;
  }
  // 默认麦 (eCapture, eConsole)
  MComPtr<IMMDevice> pDefaultCapture;
  hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole,
                                            &pDefaultCapture);
  LPWSTR defaultCaptureId = nullptr;
  if (SUCCEEDED(hr) && pDefaultCapture) {
    pDefaultCapture->GetId(&defaultCaptureId);
  }
  // 默认声卡回环 (eRender, eConsole): 默认播放设备的回环采集
  MComPtr<IMMDevice> pDefaultRender;
  hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultRender);
  LPWSTR defaultRenderId = nullptr;
  if (SUCCEEDED(hr) && pDefaultRender) {
    pDefaultRender->GetId(&defaultRenderId);
  }
  MComPtr<IMMDeviceCollection> pCollection;
  hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                       &pCollection);
  if (FAILED(hr)) {
    log(LogLevel::warn, "Failed to enum audio endpoints:", hr);
    if (defaultCaptureId) CoTaskMemFree(defaultCaptureId);
    if (defaultRenderId) CoTaskMemFree(defaultRenderId);
    return;
  }
  UINT count = 0;
  hr = pCollection->GetCount(&count);
  if (FAILED(hr)) {
    if (defaultCaptureId) CoTaskMemFree(defaultCaptureId);
    if (defaultRenderId) CoTaskMemFree(defaultRenderId);
    return;
  }
  // 1. 默认麦
  if (pDefaultCapture && defaultCaptureId) {
    log(LogLevel::info, "WASAPI found default capture device");
    devices.push_back(std::make_shared<WasMicSource>(pDefaultCapture));
  }
  // 2. 默认声卡回环
  if (pDefaultRender) {
    log(LogLevel::info, "WASAPI found default render device(loopback)");
    devices.push_back(std::make_shared<WasLoopbackSource>(pDefaultRender));
  }
  // 3. 余下的麦 (跳过默认麦)
  for (UINT i = 0; i < count; i++) {
    MComPtr<IMMDevice> pDevice;
    hr = pCollection->Item(i, &pDevice);
    if (FAILED(hr)) continue;
    if (defaultCaptureId) {
      LPWSTR pId = nullptr;
      pDevice->GetId(&pId);
      bool isDefault = (pId && wcscmp(pId, defaultCaptureId) == 0);
      CoTaskMemFree(pId);
      if (isDefault) continue;
    }
    log(LogLevel::info, "WASAPI found capture device ", i);
    devices.push_back(std::make_shared<WasMicSource>(pDevice));
  }
  if (defaultCaptureId) CoTaskMemFree(defaultCaptureId);
  if (defaultRenderId) CoTaskMemFree(defaultRenderId);
}

}
