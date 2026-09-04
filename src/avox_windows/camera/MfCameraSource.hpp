#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include "MfCameraFormat.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/VideoSource.hpp"
#include "avox_windows/WinCommon.hpp"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace avox {

// Windows Media Foundation相机设备源, 移植自aoce_win_mf
// MF异步回调线程只做拷贝入队, 推帧统一在RunTask线程(渲染发生在onFrame调用线程)
class MfCameraSource : public VideoSource, public RunTask,
                       public IMFSourceReaderCallback {
 public:
  MfCameraSource() = default;
  virtual ~MfCameraSource() override;

 private:
  MComPtr<IMFActivate> activate = nullptr;
  MComPtr<IMFMediaSource> source = nullptr;
  MComPtr<IMFSourceReader> sourceReader = nullptr;
  // init时获取, open时按mfIndex设置source输出格式
  MComPtr<IMFMediaTypeHandler> handler = nullptr;
  // 异步模式必须禁用内置转换器, 帧回调走IMFSourceReaderCallback
  MComPtr<IMFAttributes> readerAttrs = nullptr;
  // 与descList同序的MF格式表(带原始media type索引)
  std::vector<MfCameraFormat> mfFormats = {};
  int32_t selectIndex = 0;
  int32_t videoIndex = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
  // reader输出行距(字节), 打开格式时从MF_MT_DEFAULT_STRIDE读取
  int32_t frameStride = 0;
  // 保护open/close全流程, setPlay假定调用方已持锁
  std::mutex playMtx;
  std::mutex flushMtx;
  std::condition_variable flushSignal;
  // 帧队列: MF回调线程入队, RunTask线程出队推帧, 上限3帧丢最旧
  std::mutex frameMtx;
  std::condition_variable frameCv;
  std::deque<std::vector<uint8_t>> frameQueue;
  long refCount = 0;

 public:
  // 激活设备并枚举格式, 失败(如MF读不了的采集卡)返回false不入设备列表
  bool init(IMFActivate* activate);
  // 断流上报
  void lostDevice();

 protected:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
  virtual void onRunTask() override;

 private:
  // (重)建source与reader
  bool ensureReader();
  // 按curDesc在mfFormats里找最匹配的索引
  int32_t findFormatIndex();
  // 启停流, Flush同步等OnFlush(3秒超时), 防止释放reader时回调在途
  bool setPlay(bool play);
  // MF回调线程拷贝入队
  void pushFrame(const uint8_t* data, int32_t size);

 public:
  // IMFSourceReaderCallback, AddRef/Release为桩, 生命周期由Mgr的shared_ptr管理
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                                   void** ppvObject) override;
  virtual ULONG STDMETHODCALLTYPE AddRef() override;
  virtual ULONG STDMETHODCALLTYPE Release() override;
  virtual HRESULT STDMETHODCALLTYPE OnReadSample(
      HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags,
      LONGLONG llTimestamp, IMFSample* pSample) override;
  virtual HRESULT STDMETHODCALLTYPE OnFlush(DWORD dwStreamIndex) override;
  virtual HRESULT STDMETHODCALLTYPE OnEvent(DWORD dwStreamIndex,
                                            IMFMediaEvent* pEvent) override;
};

class MfCameraSourceMgr : public VideoManager<MfCameraSource> {
 public:
  MfCameraSourceMgr();
  virtual ~MfCameraSourceMgr();

 private:
  bool mfStarted = false;

 protected:
  virtual void onInitDevices() override;
};

// 注册到VDeviceSdk::win_mf, 由AvoxManager::init显式调用(防静态链接丢弃)
void regWinMfCameraDevice();

}  // namespace avox
