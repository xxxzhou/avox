#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxBase.h"
#include "avox/module/RunTask.hpp"
#include "TorrentEngine.hpp"

namespace avox {

// ISourceProbe 实现(avox_torrent): 包装 TorrentEngine::probe 的异步文件列表探测。
// 线程模型: start() 起工作线程跑 engine->probe(阻塞有界), 完成后先把结果拷成
// 纯值成员再回调 ob(探测线程回调, 上层自行切线程); stop() 置 abort 快速打断等待。
class TorrentProbe : public ISourceProbe, public RunTask {
 public:
  TorrentProbe();
  // 析构定义在cpp: engine 持有前向声明类型的 unique_ptr, 需完整类型可见处销毁
  virtual ~TorrentProbe();

 public:
  virtual void setOb(ISourceProbeOb* ob) override;
  virtual bool start(const char* url, const char* cacheDir,
                     int32_t timeoutMs) override;
  virtual void stop() override;
  virtual bool probing() override;

  virtual int32_t getFileCount() override;
  virtual int32_t getFileIndex(int32_t i) override;
  virtual const char* getFilePath(int32_t i) override;
  virtual uint64_t getFileSize(int32_t i) override;
  virtual bool isMediaFile(int32_t i) override;
  virtual const char* getName() override;
  virtual const char* getInfoHash() override;
  virtual uint64_t getTotalSize() override;
  virtual const char* getLastError() override;

  virtual void selectFile(int32_t fileIndex) override;
  virtual int32_t getSelectedIndex() override;
  virtual bool applyToOption(IOption* option) override;

 protected:
  // 工作线程: 跑 engine->probe + 拷结果 + 回调(abort 时静默退出)
  virtual void onRunTask() override;

 private:
  // 结果条目(种子内原始 index + 路径/大小/媒体标注)
  struct Item {
    int32_t index = -1;
    std::string path = "";
    uint64_t size = 0;
    bool media = false;
  };

  // 观察者(单播, 探测线程回调)
  std::atomic<ISourceProbeOb*> obAt{nullptr};
  // 请求参数(start 快照)
  std::string reqUrl;
  TorrentEngine::Config cfg = {};
  // 探测引擎(仅工作线程构造/使用/销毁)
  std::unique_ptr<TorrentEngine> engine;
  // 外部中止源(stop 置位 -> engine 等元数据 50ms 内退出)
  std::atomic<bool> abortFlag{false};
  // 探测结果(结果锁: 工作线程写, getter 读)
  std::mutex resultMutex;
  std::vector<Item> items;
  std::string torrentName;
  std::string infoHash;
  uint64_t totalSize = 0;
  int32_t selectedIndex = -1;
  std::string lastError;
};
}
