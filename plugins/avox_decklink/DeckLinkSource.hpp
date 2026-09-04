#pragma once

#include <DeckLinkAPI.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include "avox/module/RunTask.hpp"
#include "avox/source/VideoSource.hpp"
#include "avox_windows/WinCommon.hpp"
#include "decklink_sdk/DeckLinkAPIVersion.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace avox {

// DeckLink(Blackmagic)采集卡设备源, 移植自aoce_decklink
// 信号制式跟随: 支持FormatDetection的设备带bmdVideoInputEnableFormatDetection打开,
// 输入信号变化由VideoInputFormatChanged重启; 帧固定bmdFormat8BitYUV(uyvyI)
// DeckLink回调线程只做拷贝入队, 推帧统一在RunTask线程(渲染发生在onFrame调用线程)
class DeckLinkSource : public VideoSource, public RunTask,
                       public IDeckLinkInputCallback {
 public:
  DeckLinkSource() = default;
  virtual ~DeckLinkSource() override;

 public:
  // 填设备名/格式列表, 失败返回false不入设备列表
  bool init(IDeckLink* deckLink);
  // 断流上报
  void lostDevice();

 protected:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
  virtual void onRunTask() override;

 private:
  // 帧项: 自带宽高行距(格式自适应可能中途变化)
  struct FrameItem {
    std::vector<uint8_t> data;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;
  };

  MComPtr<IDeckLink> deckLink = nullptr;
  MComPtr<IDeckLinkInput> input = nullptr;
  // 与descList同序的display mode表
  std::vector<MComPtr<IDeckLinkDisplayMode>> modeList = {};
  int32_t selectIndex = 0;
  // 是否支持输入信号格式自适应
  BOOL bFormatDetection = FALSE;
  // 保护open/close/格式切换, 格式回调以bOpen判重入
  std::mutex playMtx;
  // 帧队列: DeckLink回调线程入队, RunTask线程出队推帧, 上限3帧丢最旧
  std::mutex frameMtx;
  std::condition_variable frameCv;
  std::deque<FrameItem> frameQueue;
  long refCount = 0;

 private:
  // 按curDesc在modeList里找最接近的display mode索引
  int32_t findModeIndex();
  // DeckLink回调线程拷贝入队
  void pushFrame(const uint8_t* data, int32_t size, int32_t width,
                 int32_t height, int32_t stride);

 public:
  // IDeckLinkInputCallback, AddRef/Release为桩, 生命周期归Mgr的shared_ptr
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                                   LPVOID* ppv) override;
  virtual ULONG STDMETHODCALLTYPE AddRef() override;
  virtual ULONG STDMETHODCALLTYPE Release() override;
  virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
      IDeckLinkVideoInputFrame* videoFrame,
      IDeckLinkAudioInputPacket* audioPacket) override;
  virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
      BMDVideoInputFormatChangedEvents notificationEvents,
      IDeckLinkDisplayMode* newDisplayMode,
      BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;
};

class DeckLinkSourceMgr : public VideoManager<DeckLinkSource> {
 public:
  DeckLinkSourceMgr();
  virtual ~DeckLinkSourceMgr();

 private:
  // 本对象是否拥有线程COM初始化(配对CoUninitialize)
  bool coInited = false;

 protected:
  virtual void onInitDevices() override;
};

}  // namespace avox
