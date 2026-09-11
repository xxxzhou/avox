#include "TorrentProbe.hpp"

namespace avox {

TorrentProbe::TorrentProbe() {}

// 析构在完整类型可见处定义(engine 的 unique_ptr 删除器)
TorrentProbe::~TorrentProbe() { stop(); }

void TorrentProbe::setOb(ISourceProbeOb* ob) { obAt.store(ob); }

bool TorrentProbe::start(const char* url, const char* cacheDir,
                         int32_t timeoutMs) {
  if (running()) {
    return false;
  }
  reqUrl = url ? url : "";
  cfg = TorrentEngine::Config{};
  cfg.cacheDir = cacheDir ? cacheDir : "";
  cfg.metaTimeoutMs = timeoutMs > 0 ? timeoutMs : cfg.metaTimeoutMs;
  // 清空上一轮结果(选择保留与否以新一轮为准, 重置)
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    items.clear();
    torrentName = "";
    infoHash = "";
    totalSize = 0;
    lastError = "";
    selectedIndex = -1;
  }
  abortFlag.store(false);
  taskName = "torrent probe";
  startTask();
  return true;
}

void TorrentProbe::stop() {
  // 置中止即可: 工作线程在 engine->probe 内 50ms 步进检查 abortFlag 快速退出,
  // 并自行做引擎 shutdown/结果清理; 此处 stopTask 只负责 join。
  // (不在此处碰 engine: 它归工作线程所有, 跨线程动指针有竞态)
  abortFlag.store(true);
  if (running()) {
    stopTask();
  }
}

bool TorrentProbe::probing() { return running(); }

void TorrentProbe::onRunTask() {
  engine = std::make_unique<TorrentEngine>();
  engine->setAbortFlag(&abortFlag);
  std::string err;
  bool ok = engine->probe(reqUrl, cfg, &err);
  // 拷贝成纯值结果(引擎内成员本就是纯值, 关会话后仍可读)
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    for (const auto& fi : engine->getFileList()) {
      Item it;
      it.index = fi.index;
      it.path = fi.path;
      it.size = fi.size;
      it.media = TorrentEngine::isMediaPath(fi.path);
      items.push_back(std::move(it));
    }
    torrentName = engine->getTorrentName();
    infoHash = engine->getInfoHash();
    totalSize = engine->getTotalSize();
    if (!ok) {
      lastError = err;
    }
  }
  engine.reset();
  // 上层已 stop: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  ISourceProbeOb* o = obAt.load();
  if (o) {
    o->onProbeResult(ok ? 0 : -1);
  }
}

int32_t TorrentProbe::getFileCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)items.size();
}

int32_t TorrentProbe::getFileIndex(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)items.size()) ? items[i].index : -1;
}

const char* TorrentProbe::getFilePath(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)items.size()) ? items[i].path.c_str() : "";
}

uint64_t TorrentProbe::getFileSize(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)items.size()) ? items[i].size : 0;
}

bool TorrentProbe::isMediaFile(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)items.size()) ? items[i].media : false;
}

const char* TorrentProbe::getName() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return torrentName.c_str();
}

const char* TorrentProbe::getInfoHash() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return infoHash.c_str();
}

uint64_t TorrentProbe::getTotalSize() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return totalSize;
}

const char* TorrentProbe::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

void TorrentProbe::selectFile(int32_t fileIndex) {
  std::lock_guard<std::mutex> lk(resultMutex);
  selectedIndex = fileIndex;
}

int32_t TorrentProbe::getSelectedIndex() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return selectedIndex;
}

bool TorrentProbe::applyToOption(IOption* option) {
  if (option == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (selectedIndex < 0) {
    return false;
  }
  option->setInt("torrent.fileIndex", selectedIndex);
  return true;
}

}
