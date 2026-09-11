#include "TorrentSource.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace avox {

TorrentSource::TorrentSource() {}

// 析构在完整类型可见处定义(engine 的 unique_ptr 删除器)
TorrentSource::~TorrentSource() { close(); }

void TorrentSource::setOb(IRemoteSourceOb* ob) { obAt.store(ob); }

uint32_t TorrentSource::getCaps() {
  // 元数据获取无分进度回调(engine 只有有界等待), 搜索/缩略图/状态均不支持
  return 0;
}

RemoteAuthKind TorrentSource::getAuthKind() { return RemoteAuthKind::none; }

void TorrentSource::setParam(const char* key, const char* value) {
  if (key == nullptr) {
    return;
  }
  // 与播放选项 torrent.* 同义的会话参数, 传同值可让起播命中元数据缓存
  if (std::strcmp(key, "cacheDir") == 0) {
    paramCacheDir = value ? value : "";
  } else if (std::strcmp(key, "extraTrackers") == 0) {
    paramExtraTrackers = value ? value : "";
  }
}

bool TorrentSource::open(const char* url, const char* user, const char* pass,
                         const char* token, int32_t timeoutMs) {
  (void)user;
  (void)pass;
  (void)token;  // 磁力无鉴权
  if (running()) {
    return false;
  }
  if (url == nullptr || *url == '\0') {
    return false;
  }
  reqUrl = url;
  cfg = TorrentEngine::Config{};
  cfg.cacheDir = paramCacheDir;
  cfg.extraTrackers = paramExtraTrackers;
  cfg.metaTimeoutMs = timeoutMs > 0 ? timeoutMs : cfg.metaTimeoutMs;
  // 清空上一轮会话结果
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    files.clear();
    tree.clear();
    batch.clear();
    torrentName = "";
    infoHash = "";
    totalSize = 0;
    lastError = "";
  }
  openedFlag.store(false);
  abortFlag.store(false);
  op = Op::open;
  taskName = "torrent open";
  startTask();
  return true;
}

void TorrentSource::close() {
  // 置中止即可: 工作线程在 engine->probe 内 50ms 步进检查 abortFlag 快速退出,
  // 并自行做引擎 shutdown; stopTask 只负责 join (engine 归工作线程所有, 不跨线程碰)
  abortFlag.store(true);
  openedFlag.store(false);
  if (running()) {
    stopTask();
  }
  // 清会话结果(线程已停或未跑)
  std::lock_guard<std::mutex> lk(resultMutex);
  files.clear();
  tree.clear();
  batch.clear();
  torrentName = "";
  infoHash = "";
  totalSize = 0;
  lastError = "";
}

bool TorrentSource::opened() { return openedFlag.load(); }

bool TorrentSource::list(const char* nodeToken, int32_t timeoutMs) {
  (void)timeoutMs;  // 树在 open 时已构建, 本地枚举即时完成
  if (running() || !openedFlag.load()) {
    return false;
  }
  listToken = nodeToken ? nodeToken : "";
  abortFlag.store(false);
  op = Op::list;
  taskName = "torrent list";
  startTask();
  return true;
}

void TorrentSource::stopList() {
  abortFlag.store(true);
  if (running()) {
    stopTask();
  }
}

void TorrentSource::onRunTask() {
  if (op == Op::open) {
    runOpen();
  } else {
    runList();
  }
}

void TorrentSource::runOpen() {
  auto engine = std::make_unique<TorrentEngine>();
  engine->setAbortFlag(&abortFlag);
  std::string err;
  bool ok = engine->probe(reqUrl, cfg, &err);
  // 拷贝成纯值结果(引擎内成员本就是纯值, 关会话后仍可读)
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = ok ? "" : err;
    for (const auto& fi : engine->getFileList()) {
      FileEntry fe;
      fe.index = fi.index;
      fe.path = fi.path;
      fe.size = fi.size;
      fe.media = TorrentEngine::isMediaPath(fi.path);
      files.push_back(std::move(fe));
    }
    torrentName = engine->getTorrentName();
    infoHash = engine->getInfoHash();
    totalSize = engine->getTotalSize();
    buildTree();
  }
  if (ok) {
    openedFlag.store(true);
  }
  engine.reset();
  // 上层已 close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  int32_t code = (int32_t)RemoteCode::ok;
  if (!ok) {
    if (err.find("aborted") != std::string::npos) {
      code = (int32_t)RemoteCode::canceled;
    } else if (err.find("timeout") != std::string::npos) {
      code = (int32_t)RemoteCode::timeout;
    } else {
      code = (int32_t)RemoteCode::other;
    }
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onOpenResult(code);
  }
}

void TorrentSource::runList() {
  int32_t code = (int32_t)RemoteCode::ok;
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    auto it = tree.find(listToken);
    if (it == tree.end()) {
      lastError = "node not found: " + listToken;
      code = (int32_t)RemoteCode::notFound;
    } else {
      batch = it->second;
    }
  }
  // 上层已 stopList/close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onListResult(code);
  }
}

std::string TorrentSource::parentToken(const std::string& dirToken) {
  // 去掉尾部 '/' 后找上一级 '/'
  std::string dir = dirToken.substr(0, dirToken.size() - 1);
  size_t pos = dir.find_last_of('/');
  return pos == std::string::npos ? "" : dir.substr(0, pos + 1);
}

void TorrentSource::buildTree() {
  tree.clear();
  std::set<std::string> dirs;             // 所有目录 token(带尾'/')
  std::map<std::string, uint64_t> dirSizes;  // 目录 token -> 子树合计
  // 第一遍: 登记每级祖先目录并累计子树大小
  for (const auto& fe : files) {
    std::string path = fe.path;
    std::replace(path.begin(), path.end(), '\\', '/');
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
      continue;  // 根下文件, 无祖先目录
    }
    std::string dir = path.substr(0, pos + 1);
    while (!dir.empty()) {
      dirs.insert(dir);
      dirSizes[dir] += fe.size;
      dir = parentToken(dir);
    }
  }
  // 第二遍: 目录挂到父节点
  for (const auto& d : dirs) {
    Entry e;
    e.type = RemoteEntryType::dir;
    std::string noSlash = d.substr(0, d.size() - 1);
    size_t pos = noSlash.find_last_of('/');
    e.name = pos == std::string::npos ? noSlash : noSlash.substr(pos + 1);
    e.token = d;
    auto sz = dirSizes.find(d);
    e.size = sz != dirSizes.end() ? sz->second : 0;
    tree[parentToken(d)].push_back(std::move(e));
  }
  // 第三遍: 文件挂到所在目录
  for (const auto& fe : files) {
    Entry e;
    std::string path = fe.path;
    std::replace(path.begin(), path.end(), '\\', '/');
    size_t pos = path.find_last_of('/');
    e.name = pos == std::string::npos ? path : path.substr(pos + 1);
    e.type = fe.media ? RemoteEntryType::media : RemoteEntryType::file;
    e.token = path;
    e.size = fe.size;
    e.fileIndex = fe.index;
    tree[pos == std::string::npos ? "" : path.substr(0, pos + 1)].push_back(
        std::move(e));
  }
  // 排序: 目录在前, 同类按名称升序
  for (auto& kv : tree) {
    std::stable_sort(kv.second.begin(), kv.second.end(),
                     [](const Entry& a, const Entry& b) {
                       if (a.type != b.type) {
                         return a.type == RemoteEntryType::dir;
                       }
                       return a.name < b.name;
                     });
  }
}

int32_t TorrentSource::getEntryCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)batch.size();
}

RemoteEntryType TorrentSource::getEntryType(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].type
                                               : RemoteEntryType::other;
}

const char* TorrentSource::getEntryName(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].name.c_str() : "";
}

uint64_t TorrentSource::getEntrySize(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].size : 0;
}

const char* TorrentSource::getEntryToken(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].token.c_str() : "";
}

const char* TorrentSource::getSessionField(const char* key) {
  if (key == nullptr) {
    return "";
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (std::strcmp(key, "name") == 0) {
    return torrentName.c_str();
  }
  if (std::strcmp(key, "infoHash") == 0) {
    return infoHash.c_str();
  }
  if (std::strcmp(key, "totalSize") == 0) {
    totalSizeBuf = std::to_string(totalSize);
    return totalSizeBuf.c_str();
  }
  return "";
}

const char* TorrentSource::resolve(int32_t entryIndex, IOption* option) {
  std::lock_guard<std::mutex> lk(resultMutex);
  if (entryIndex < 0 || entryIndex >= (int32_t)batch.size()) {
    lastError = "entry index out of range";
    return nullptr;
  }
  const Entry& e = batch[entryIndex];
  // 目录暂不解析(蓝光原盘等容器翻译留给后续), 仅媒体文件可播
  if (e.type != RemoteEntryType::media || e.fileIndex < 0) {
    lastError = "entry not playable media: " + e.token;
    return nullptr;
  }
  if (option) {
    option->setInt("torrent.fileIndex", e.fileIndex);
  }
  resolvedBuf = reqUrl;
  return resolvedBuf.c_str();
}

const char* TorrentSource::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

}
