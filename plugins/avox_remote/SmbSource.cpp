#include "SmbSource.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace avox {

namespace {

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

bool isMediaPath(const std::string& path) {
  static const char* kExts[] = {".mp4", ".mkv", ".ts",  ".flv", ".webm",
                                ".avi", ".mov", ".m4v", ".mpg", ".mpeg",
                                ".wmv", ".3gp", ".rm",  ".rmvb"};
  size_t dot = path.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = toLowerCopy(path.substr(dot));
  for (auto* e : kExts) {
    if (ext == e) {
      return true;
    }
  }
  return false;
}

}  // namespace

SmbSource::SmbSource() {}

SmbSource::~SmbSource() { close(); }

void SmbSource::setOb(IRemoteSourceOb* ob) { obAt.store(ob); }

uint32_t SmbSource::getCaps() {
  // 纯 SMB 协议无服务端搜索/缩略图/播放状态
  return 0;
}

RemoteAuthKind SmbSource::getAuthKind() { return RemoteAuthKind::userPass; }

void SmbSource::setParam(const char* key, const char* value) {
  (void)key;
  (void)value;
  // v1 无会话级参数(账密走 open 参数/URL userinfo; 超时走 timeoutMs)
}

bool SmbSource::parseUrl(const std::string& url, UrlParts* out) {
  const std::string kScheme = "smb://";
  if (url.rfind(kScheme, 0) != 0) {
    return false;
  }
  size_t rest = kScheme.size();
  size_t pathStart = url.find('/', rest);
  std::string authority =
      url.substr(rest, pathStart == std::string::npos ? std::string::npos
                                                      : pathStart - rest);
  std::string path =
      pathStart == std::string::npos ? "" : url.substr(pathStart + 1);
  // userinfo(取 host 前最后一个 '@'): user[:pass]
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    std::string ui = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = ui.find(':');
    if (colon == std::string::npos) {
      out->user = ui;
    } else {
      out->user = ui.substr(0, colon);
      out->pass = ui.substr(colon + 1);
    }
  }
  // host[:port] (IPv6 简单支持 [..]:port)
  if (!authority.empty() && authority[0] == '[') {
    size_t rb = authority.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, rb - 1);
    if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
      out->port = std::atoi(authority.c_str() + rb + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos &&
        authority.find(':') == colon) {  // 单冒号才当端口, 防裸 IPv6
      out->host = authority.substr(0, colon);
      out->port = std::atoi(authority.c_str() + colon + 1);
    } else {
      out->host = authority;
    }
  }
  if (out->host.empty()) {
    return false;
  }
  if (out->port == 0) {
    out->port = 445;
  }
  // 第一段路径 = 共享名(必填); 其余为初始路径
  if (path.empty() || path == "/") {
    return false;  // 无 share: 枚举共享列表属 srvsvc 范畴, v1 不做
  }
  if (path.back() == '/') {
    path.pop_back();
  }
  size_t slash = path.find('/');
  if (slash == std::string::npos) {
    out->share = path;
    out->rootToken = "/";
  } else {
    out->share = path.substr(0, slash);
    out->rootToken = path.substr(slash);  // 保留首'/', 目录带尾'/'
    if (out->rootToken.back() != '/') {
      out->rootToken += '/';
    }
  }
  if (out->share.empty()) {
    return false;
  }
  return true;
}

bool SmbSource::open(const char* url, const char* user, const char* pass,
                     const char* token, int32_t timeoutMs) {
  (void)token;  // smb 无 token 形态鉴权 (NTLM 走 user/pass, domain 在 user 内)
  if (running()) {
    return false;
  }
  if (url == nullptr || *url == '\0') {
    return false;
  }
  auto p = std::make_unique<UrlParts>();
  if (!parseUrl(url, p.get())) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "unsupported url(expect smb://host[:port]/share[/path])";
    return false;
  }
  if (user != nullptr && *user != '\0') {
    p->user = user;
    p->pass = pass != nullptr ? pass : "";
  }
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : 10000;
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    batch.clear();
    sessionName = "";
    lastError = "";
    resolvedBuf = "";
  }
  parts = std::move(p);  // startTask 前写完, 工作线程读无竞态
  openedFlag.store(false);
  abortFlag.store(false);
  op = Op::open;
  taskName = "smb open";
  startTask();
  return true;
}

void SmbSource::close() {
  // libsmb2 同步调用不可中途打断: 当前调用返回后工作线程见 abortFlag 丢弃结果退出
  abortFlag.store(true);
  openedFlag.store(false);
  if (running()) {
    stopTask();
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  batch.clear();
  sessionName = "";
  lastError = "";
  resolvedBuf = "";
}

bool SmbSource::opened() { return openedFlag.load(); }

bool SmbSource::list(const char* nodeToken, int32_t timeoutMs) {
  if (running() || !openedFlag.load()) {
    return false;
  }
  listToken = nodeToken != nullptr ? nodeToken : "";
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : opTimeoutMs;
  abortFlag.store(false);
  op = Op::list;
  taskName = "smb list";
  startTask();
  return true;
}

void SmbSource::stopList() {
  abortFlag.store(true);
  if (running()) {
    stopTask();
  }
}

bool SmbSource::fillBatch(struct smb2_context* ctx, struct smb2dir* dir,
                          const std::string& dirToken) {
  (void)ctx;
  batch.clear();
  std::vector<Entry> items;
  struct smb2dirent* ent = nullptr;
  while ((ent = smb2_readdir(ctx, dir)) != nullptr) {
    if (std::strcmp(ent->name, ".") == 0 || std::strcmp(ent->name, "..") == 0) {
      continue;
    }
    Entry e;
    bool isDir = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;
    e.name = ent->name;
    // token 是 share 内绝对路径(含 rootToken 前缀), 下钻/resolve 直接可用
    e.token = dirToken + ent->name + (isDir ? "/" : "");
    e.type = isDir ? RemoteEntryType::dir
                   : (isMediaPath(e.name) ? RemoteEntryType::media
                                          : RemoteEntryType::file);
    e.size = isDir ? 0 : (uint64_t)ent->st.smb2_size;
    items.push_back(std::move(e));
  }
  // 目录在前, 同类名称升序
  std::stable_sort(items.begin(), items.end(),
                   [](const Entry& a, const Entry& b) {
                     if (a.type != b.type) {
                       return a.type == RemoteEntryType::dir;
                     }
                     return a.name < b.name;
                   });
  batch = std::move(items);
  return !batch.empty();
}

void SmbSource::runOpen() {
  const UrlParts& p = *parts;
  struct smb2_context* ctx = smb2_init_context();
  int32_t code = (int32_t)RemoteCode::ok;
  if (!ctx) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "smb2_init_context failed";
    code = (int32_t)RemoteCode::other;
  } else {
    smb2_set_timeout(ctx, std::max<int32_t>(1, (opTimeoutMs + 999) / 1000));
    if (!p.user.empty()) {
      smb2_set_user(ctx, p.user.c_str());
      smb2_set_password(ctx, p.pass.c_str());
    }
    // 阻塞连接(RunTask 工作线程); 失败原因经 smb2_geterror 可读
    int ret = smb2_connect_share(ctx, p.host.c_str(), p.share.c_str(),
                                 p.user.empty() ? nullptr : p.user.c_str());
    if (ret != 0) {
      std::string err = smb2_get_error(ctx);
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = "connect share failed: " + err;
      if (err.find("LOGON") != std::string::npos ||
          err.find("ACCESS_DENIED") != std::string::npos ||
          err.find("BAD_USERID") != std::string::npos ||
          err.find("PASSWORD_EXPIRED") != std::string::npos) {
        code = (int32_t)RemoteCode::authFailed;
      } else {
        code = (int32_t)RemoteCode::net;
      }
      smb2_destroy_context(ctx);
    } else {
      std::lock_guard<std::mutex> lk(resultMutex);
      sessionName = p.share;
      lastError = "";
      openedFlag.store(true);
      // 会话是无状态的: 每个 op 各建/各销 ctx(连接成本一次 TCP, 免跨线程生命周期问题)
      smb2_destroy_context(ctx);
    }
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

void SmbSource::runList() {
  const UrlParts& p = *parts;
  // 顶层"/"映射到会话入口子目录(rootToken); 其余 token 即 share 内绝对路径
  std::string dir = listToken.empty() ? "/" : listToken;
  if ((dir == "/" || dir.empty()) && p.rootToken != "/") {
    dir = p.rootToken;
  }
  if (!isDirToken(dir)) {
    dir += "/";  // 容错: 目录 token 必须带尾'/'
  }
  // share 内路径: 去首'/'(libsmb2 以 share 为根, '/'分隔)
  std::string sharePath = dir;
  if (!sharePath.empty() && sharePath[0] == '/') {
    sharePath = sharePath.substr(1);
  }
  if (sharePath.size() > 1 && sharePath.back() == '/') {
    sharePath.pop_back();  // opendir 要的是不带尾'/'的路径
  }

  struct smb2_context* ctx = smb2_init_context();
  int32_t code = (int32_t)RemoteCode::ok;
  if (!ctx) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "smb2_init_context failed";
    code = (int32_t)RemoteCode::other;
  } else {
    smb2_set_timeout(ctx, std::max<int32_t>(1, (opTimeoutMs + 999) / 1000));
    if (!p.user.empty()) {
      smb2_set_user(ctx, p.user.c_str());
      smb2_set_password(ctx, p.pass.c_str());
    }
    int ret = smb2_connect_share(ctx, p.host.c_str(), p.share.c_str(),
                                 p.user.empty() ? nullptr : p.user.c_str());
    struct smb2dir* d = nullptr;
    if (ret == 0) {
      d = smb2_opendir(ctx, sharePath.c_str());
      if (d == nullptr) {
        std::string err = smb2_get_error(ctx);
        std::lock_guard<std::mutex> lk(resultMutex);
        lastError = "opendir " + dir + " failed: " + err;
        code = err.find("NOT_FOUND") != std::string::npos
                   ? (int32_t)RemoteCode::notFound
                   : (int32_t)RemoteCode::other;
      }
    } else {
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = std::string("connect share failed: ") + smb2_get_error(ctx);
      code = (int32_t)RemoteCode::net;
    }
    if (d != nullptr) {
      bool ok = fillBatch(ctx, d, dir);
      smb2_closedir(ctx, d);
      std::lock_guard<std::mutex> lk(resultMutex);
      if (!ok) {
        lastError = "no entries under " + dir;
        code = (int32_t)RemoteCode::notFound;
      }
    }
    smb2_disconnect_share(ctx);
    smb2_destroy_context(ctx);
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

void SmbSource::onRunTask() {
  if (op == Op::open) {
    runOpen();
  } else {
    runList();
  }
}

std::string SmbSource::baseName(const std::string& token) {
  std::string noSlash = token;
  if (noSlash.size() > 1 && noSlash.back() == '/') {
    noSlash.pop_back();
  }
  size_t pos = noSlash.find_last_of('/');
  return pos == std::string::npos ? noSlash : noSlash.substr(pos + 1);
}

bool SmbSource::isDirToken(const std::string& token) {
  return !token.empty() && token.back() == '/';
}

std::string SmbSource::entryUrl(const std::string& token) const {
  const UrlParts& p = *parts;
  std::string auth;
  if (!p.user.empty()) {
    auth = p.user + ":" + p.pass + "@";
  }
  std::string port;
  if (p.port != 445) {
    port = ":" + std::to_string(p.port);
  }
  // token 是 share 内绝对路径(首'/'开头): 拼成 smb://host[:port]/share/path
  std::string path = token;
  if (!path.empty() && path.back() == '/') {
    path.pop_back();
  }
  return "smb://" + auth + p.host + port + "/" + p.share + path;
}

int32_t SmbSource::getEntryCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)batch.size();
}

RemoteEntryType SmbSource::getEntryType(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].type
                                               : RemoteEntryType::other;
}

const char* SmbSource::getEntryName(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].name.c_str() : "";
}

uint64_t SmbSource::getEntrySize(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].size : 0;
}

const char* SmbSource::getEntryToken(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].token.c_str() : "";
}

const char* SmbSource::getSessionField(const char* key) {
  if (key == nullptr) {
    return "";
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (std::strcmp(key, "name") == 0) {
    return sessionName.c_str();
  }
  if (parts && std::strcmp(key, "host") == 0) {
    return parts->host.c_str();
  }
  return "";
}

const char* SmbSource::resolve(int32_t entryIndex, IOption* option) {
  (void)option;  // smb 播放无需私有 option 键 (鉴权已嵌 userinfo)
  std::lock_guard<std::mutex> lk(resultMutex);
  if (entryIndex < 0 || entryIndex >= (int32_t)batch.size()) {
    lastError = "entry index out of range";
    return nullptr;
  }
  const Entry& e = batch[entryIndex];
  if (e.type == RemoteEntryType::dir) {
    lastError = "entry is a directory(先 list 下钻): " + e.token;
    return nullptr;
  }
  resolvedBuf = entryUrl(e.token);
  return resolvedBuf.c_str();
}

const char* SmbSource::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

}
