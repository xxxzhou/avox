#include "DiscSource.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <libbluray/bluray.h>

namespace avox {

namespace {
// 90kHz ticks -> "H:MM:SS"
std::string formatTicks(uint64_t ticks) {
  uint64_t s = ticks / 90000;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                (unsigned)(s / 3600), (unsigned)((s / 60) % 60),
                (unsigned)(s % 60));
  return buf;
}
}

DiscSource::DiscSource() {}

DiscSource::~DiscSource() { close(); }

void DiscSource::setOb(IRemoteSourceOb* ob) { obAt.store(ob); }

uint32_t DiscSource::getCaps() { return 0; }

RemoteAuthKind DiscSource::getAuthKind() { return RemoteAuthKind::none; }

bool DiscSource::open(const char* url, const char* user, const char* pass,
                      const char* token, int32_t timeoutMs) {
  (void)user;
  (void)pass;
  (void)token;
  (void)timeoutMs;  // 本地元数据读, 无慢源语义
  if (running()) {
    return false;
  }
  if (url == nullptr || *url == '\0') {
    return false;
  }
  reqUrl = url;
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    titles.clear();
    lastError = "";
  }
  openedFlag.store(false);
  abortFlag.store(false);
  taskName = "disc open";
  startTask();
  return true;
}

void DiscSource::close() {
  abortFlag.store(true);
  openedFlag.store(false);
  if (running()) {
    stopTask();
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  titles.clear();
  lastError = "";
}

bool DiscSource::opened() { return openedFlag.load(); }

bool DiscSource::list(const char* nodeToken, int32_t timeoutMs) {
  (void)timeoutMs;
  // 标题平列表在 open 时已构建; 蓝光无目录下钻(设计约定: 绝不列 BDMV 文件树)
  if (running() || !openedFlag.load()) {
    return false;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o && (nodeToken == nullptr || *nodeToken == '\0')) {
    o->onListResult((int32_t)RemoteCode::ok);
  } else if (o) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "node not found: " + std::string(nodeToken ? nodeToken : "");
    o->onListResult((int32_t)RemoteCode::notFound);
  }
  return true;
}

void DiscSource::stopList() { abortFlag.store(true); }

void DiscSource::onRunTask() { runOpen(); }

void DiscSource::runOpen() {
  std::string err;
  int32_t code = (int32_t)RemoteCode::ok;
  std::vector<TitleEntry> probed;
  BLURAY* bd = bd_open(reqUrl.c_str(), nullptr);
  if (bd == nullptr) {
    err = "无法打开蓝光镜像: 不是 BDMV 结构(DVD/数据盘不支持)或文件不可读";
    code = (int32_t)RemoteCode::noSupport;
  } else {
    const BLURAY_DISC_INFO* di = bd_get_disc_info(bd);
    bool encrypted =
        di && ((di->aacs_detected && !di->aacs_handled) ||
               (di->bdplus_detected && !di->bdplus_handled));
    if (encrypted) {
      err = "加密蓝光(AACS/BD+)不支持, 仅支持未加密原盘";
      code = (int32_t)RemoteCode::noSupport;
    } else {
      uint32_t n = bd_get_titles(bd, 0, 0);
      for (uint32_t i = 0; i < n; ++i) {
        BLURAY_TITLE_INFO* ti = bd_get_title_info(bd, i, 0);
        if (ti == nullptr || ti->duration == 0) {
          if (ti) {
            bd_free_title_info(ti);
          }
          continue;
        }
        TitleEntry e;
        e.bdIdx = i;
        e.playlist = ti->playlist;
        e.durationTicks = ti->duration;
        bd_free_title_info(ti);
        probed.push_back(std::move(e));
      }
      if (probed.empty()) {
        err = "镜像内无可播标题(不是蓝光原盘或结构为空)";
        code = (int32_t)RemoteCode::noSupport;
      } else {
        // 时长降序: 标题 1 = 正片
        std::stable_sort(probed.begin(), probed.end(),
                         [](const TitleEntry& a, const TitleEntry& b) {
                           return a.durationTicks > b.durationTicks;
                         });
        for (size_t k = 0; k < probed.size(); ++k) {
          // locale 中立展示名(序号+时长): 上层多版本选择器直接显示, 不经翻译
          probed[k].name = "#" + std::to_string(k + 1) + " · " +
                           formatTicks(probed[k].durationTicks);
          probed[k].token = "title:" + std::to_string(probed[k].bdIdx);
        }
      }
    }
    bd_close(bd);
  }
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = err;
    titles = std::move(probed);
  }
  if (code == (int32_t)RemoteCode::ok) {
    openedFlag.store(true);
  }
  // 上层已 close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onOpenResult(code);
  }
}

int32_t DiscSource::getEntryCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)titles.size();
}

RemoteEntryType DiscSource::getEntryType(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)titles.size()) ? RemoteEntryType::media
                                                : RemoteEntryType::other;
}

const char* DiscSource::getEntryName(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)titles.size()) ? titles[i].name.c_str() : "";
}

uint64_t DiscSource::getEntrySize(int32_t i) {
  (void)i;
  return 0;  // 播放列表无字节数语义(时长在展示名内)
}

const char* DiscSource::getEntryToken(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)titles.size()) ? titles[i].token.c_str() : "";
}

const char* DiscSource::getSessionField(const char* key) {
  if (key == nullptr) {
    return "";
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (std::strcmp(key, "titleCount") == 0) {
    titleCountBuf = std::to_string(titles.size());
    return titleCountBuf.c_str();
  }
  return "";
}

const char* DiscSource::resolve(int32_t entryIndex, IOption* option) {
  std::lock_guard<std::mutex> lk(resultMutex);
  if (entryIndex < 0 || entryIndex >= (int32_t)titles.size()) {
    lastError = "entry index out of range";
    return nullptr;
  }
  if (option) {
    option->setInt("disc.title", (int32_t)titles[entryIndex].bdIdx);
  }
  resolvedBuf = reqUrl;
  return resolvedBuf.c_str();
}

const char* DiscSource::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

}
